#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>
#include <iostream>
#include <string>
#include <algorithm>

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libswscale/swscale.h>
    #include <libswresample/swresample.h>
    #include <libavutil/avutil.h>
    #include <libavutil/imgutils.h>
    #include <libavutil/channel_layout.h>
}

struct VideoPlayerState {
    bool is_open = false;
    bool is_paused = false;
    std::string filename = "";
    std::string codec_name = "None";
    
    int width = 0;
    int height = 0;
    double fps = 25.0;
    double duration_sec = 0.0;
    double current_time_sec = 0.0;

    // FFmpeg - Video
    AVFormatContext* fmt_ctx = nullptr;
    AVCodecContext* video_codec_ctx = nullptr;
    int video_stream_idx = -1;
    SwsContext* sws_ctx = nullptr;
    AVFrame* av_frame = nullptr;
    AVFrame* frame_rgba = nullptr;
    uint8_t* video_buffer = nullptr;
    SDL_Texture* texture = nullptr;

    // FFmpeg - Audio
    AVCodecContext* audio_codec_ctx = nullptr;
    int audio_stream_idx = -1;
    SwrContext* swr_ctx = nullptr;
    SDL_AudioDeviceID audio_dev = 0;
    AVFrame* audio_frame = nullptr;

    // Control & Timing
    Uint64 last_frame_ticks = 0;
    float volume = 0.6f;
};

std::string FormatTime(double seconds) {
    int total = (int)seconds;
    int m = total / 60;
    int s = total % 60;
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d", m, s);
    return std::string(buf);
}

std::string OpenFileDialog() {
    OPENFILENAMEA ofn;
    char szFile[260] = { 0 };
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Video Files\0*.mp4;*.mkv;*.avi;*.mov;*.wmv;*.flv;*.webm\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn) == TRUE) {
        return std::string(ofn.lpstrFile);
    }
    return "";
}

void CloseVideo(VideoPlayerState& state) {
    if (state.audio_dev > 0) {
        SDL_CloseAudioDevice(state.audio_dev);
        state.audio_dev = 0;
    }
    if (state.swr_ctx) { swr_free(&state.swr_ctx); }
    if (state.audio_frame) { av_frame_free(&state.audio_frame); }
    if (state.audio_codec_ctx) { avcodec_free_context(&state.audio_codec_ctx); }

    if (state.texture) { SDL_DestroyTexture(state.texture); state.texture = nullptr; }
    if (state.sws_ctx) { sws_freeContext(state.sws_ctx); state.sws_ctx = nullptr; }
    if (state.frame_rgba) { av_frame_free(&state.frame_rgba); }
    if (state.av_frame) { av_frame_free(&state.av_frame); }
    if (state.video_buffer) { av_free(state.video_buffer); state.video_buffer = nullptr; }
    if (state.video_codec_ctx) { avcodec_free_context(&state.video_codec_ctx); }

    if (state.fmt_ctx) { avformat_close_input(&state.fmt_ctx); }

    state.is_open = false;
    state.is_paused = false;
    state.current_time_sec = 0.0;
}

bool OpenVideo(const char* filepath, VideoPlayerState& state, SDL_Renderer* renderer) {
    CloseVideo(state);

    if (avformat_open_input(&state.fmt_ctx, filepath, nullptr, nullptr) < 0) return false;
    if (avformat_find_stream_info(state.fmt_ctx, nullptr) < 0) return false;

    // 1. Видеопоток
    state.video_stream_idx = av_find_best_stream(state.fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (state.video_stream_idx >= 0) {
        AVStream* stream = state.fmt_ctx->streams[state.video_stream_idx];
        const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (codec) {
            state.video_codec_ctx = avcodec_alloc_context3(codec);
            avcodec_parameters_to_context(state.video_codec_ctx, stream->codecpar);
            if (avcodec_open2(state.video_codec_ctx, codec, nullptr) >= 0) {
                state.width = state.video_codec_ctx->width;
                state.height = state.video_codec_ctx->height;
                state.filename = filepath;
                state.codec_name = codec->long_name ? codec->long_name : codec->name;

                if (stream->avg_frame_rate.den > 0) state.fps = av_q2d(stream->avg_frame_rate);
                if (state.fmt_ctx->duration != AV_NOPTS_VALUE) state.duration_sec = state.fmt_ctx->duration / (double)AV_TIME_BASE;

                state.sws_ctx = sws_getContext(
                    state.width, state.height, state.video_codec_ctx->pix_fmt,
                    state.width, state.height, AV_PIX_FMT_RGBA,
                    SWS_BILINEAR, nullptr, nullptr, nullptr
                );

                state.av_frame = av_frame_alloc();
                state.frame_rgba = av_frame_alloc();

                int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGBA, state.width, state.height, 1);
                state.video_buffer = (uint8_t*)av_malloc(numBytes * sizeof(uint8_t));
                av_image_fill_arrays(state.frame_rgba->data, state.frame_rgba->linesize, state.video_buffer, AV_PIX_FMT_RGBA, state.width, state.height, 1);

                state.texture = SDL_CreateTexture(
                    renderer, SDL_PIXELFORMAT_RGBA32,
                    SDL_TEXTUREACCESS_STREAMING, state.width, state.height
                );
            }
        }
    }

    // 2. Аудиопоток
    state.audio_stream_idx = av_find_best_stream(state.fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (state.audio_stream_idx >= 0) {
        AVStream* astream = state.fmt_ctx->streams[state.audio_stream_idx];
        const AVCodec* acodec = avcodec_find_decoder(astream->codecpar->codec_id);
        if (acodec) {
            state.audio_codec_ctx = avcodec_alloc_context3(acodec);
            avcodec_parameters_to_context(state.audio_codec_ctx, astream->codecpar);
            if (avcodec_open2(state.audio_codec_ctx, acodec, nullptr) >= 0) {
                SDL_AudioSpec wanted, have;
                SDL_zero(wanted);
                wanted.freq = state.audio_codec_ctx->sample_rate > 0 ? state.audio_codec_ctx->sample_rate : 44100;
                wanted.format = AUDIO_S16SYS;
                wanted.channels = 2;
                wanted.samples = 2048;

                state.audio_dev = SDL_OpenAudioDevice(nullptr, 0, &wanted, &have, 0);
                if (state.audio_dev > 0) {
                    SDL_PauseAudioDevice(state.audio_dev, 0);

                    if (state.audio_codec_ctx->ch_layout.nb_channels == 0) {
                        av_channel_layout_default(&state.audio_codec_ctx->ch_layout, 2);
                    }

                    AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
                    swr_alloc_set_opts2(
                        &state.swr_ctx,
                        &out_ch_layout,
                        AV_SAMPLE_FMT_S16,
                        have.freq,
                        &state.audio_codec_ctx->ch_layout,
                        state.audio_codec_ctx->sample_fmt,
                        state.audio_codec_ctx->sample_rate,
                        0, nullptr
                    );

                    swr_init(state.swr_ctx);
                    state.audio_frame = av_frame_alloc();
                }
            }
        }
    }

    state.is_open = (state.video_stream_idx >= 0);
    state.last_frame_ticks = SDL_GetTicks64();
    return state.is_open;
}

void SeekTo(VideoPlayerState& state, double target_sec) {
    if (!state.is_open) return;
    int64_t target_ts = (int64_t)(target_sec * AV_TIME_BASE);
    av_seek_frame(state.fmt_ctx, -1, target_ts, AVSEEK_FLAG_BACKWARD);
    if (state.video_codec_ctx) avcodec_flush_buffers(state.video_codec_ctx);
    if (state.audio_codec_ctx) avcodec_flush_buffers(state.audio_codec_ctx);
    if (state.audio_dev > 0) SDL_ClearQueuedAudio(state.audio_dev);
    state.current_time_sec = target_sec;
}

void AdvancePlayback(VideoPlayerState& state) {
    if (!state.is_open || state.is_paused) return;

    Uint64 current_ticks = SDL_GetTicks64();
    double frame_delay = (state.fps > 0) ? (1000.0 / state.fps) : 40.0;
    bool need_video = (current_ticks - state.last_frame_ticks) >= frame_delay;

    AVPacket packet;
    while (av_read_frame(state.fmt_ctx, &packet) >= 0) {
        if (packet.stream_index == state.video_stream_idx && need_video) {
            if (avcodec_send_packet(state.video_codec_ctx, &packet) >= 0) {
                if (avcodec_receive_frame(state.video_codec_ctx, state.av_frame) >= 0) {
                    sws_scale(
                        state.sws_ctx,
                        (const uint8_t* const*)state.av_frame->data,
                        state.av_frame->linesize,
                        0, state.height,
                        state.frame_rgba->data,
                        state.frame_rgba->linesize
                    );

                    SDL_UpdateTexture(state.texture, nullptr, state.frame_rgba->data[0], state.frame_rgba->linesize[0]);

                    if (state.av_frame->pts != AV_NOPTS_VALUE) {
                        AVStream* stream = state.fmt_ctx->streams[state.video_stream_idx];
                        state.current_time_sec = state.av_frame->pts * av_q2d(stream->time_base);
                    }

                    state.last_frame_ticks = current_ticks;
                    need_video = false;
                }
            }
        }
        else if (packet.stream_index == state.audio_stream_idx && state.audio_dev > 0) {
            if (SDL_GetQueuedAudioSize(state.audio_dev) < 44100 * 4 / 2) {
                if (avcodec_send_packet(state.audio_codec_ctx, &packet) >= 0) {
                    while (avcodec_receive_frame(state.audio_codec_ctx, state.audio_frame) >= 0) {
                        uint8_t* out_buf = nullptr;
                        int out_samples = (int)av_rescale_rnd(
                            swr_get_delay(state.swr_ctx, state.audio_codec_ctx->sample_rate) + state.audio_frame->nb_samples,
                            44100, state.audio_codec_ctx->sample_rate, AV_ROUND_UP
                        );
                        av_samples_alloc(&out_buf, nullptr, 2, out_samples, AV_SAMPLE_FMT_S16, 0);

                        int converted = swr_convert(
                            state.swr_ctx, &out_buf, out_samples,
                            (const uint8_t**)state.audio_frame->data, state.audio_frame->nb_samples
                        );

                        if (converted > 0) {
                            int data_size = converted * 2 * sizeof(int16_t);
                            int16_t* samples = (int16_t*)out_buf;
                            int total_samples = converted * 2;

                            for (int i = 0; i < total_samples; ++i) {
                                int val = (int)(samples[i] * state.volume);
                                if (val > 32767) val = 32767;
                                if (val < -32768) val = -32768;
                                samples[i] = (int16_t)val;
                            }

                            SDL_QueueAudio(state.audio_dev, out_buf, data_size);
                        }
                        if (out_buf) av_freep(&out_buf);
                    }
                }
            }
        }

        av_packet_unref(&packet);
        if (!need_video && SDL_GetQueuedAudioSize(state.audio_dev) >= 44100 * 4 / 4) break;
    }
}

SDL_Rect CalculateViewport(int win_w, int win_h, int vid_w, int vid_h) {
    if (vid_w <= 0 || vid_h <= 0) return { 0, 0, win_w, win_h };
    float win_aspect = (float)win_w / (float)win_h;
    float vid_aspect = (float)vid_w / (float)vid_h;

    SDL_Rect rect;
    if (win_aspect > vid_aspect) {
        rect.h = win_h;
        rect.w = (int)(win_h * vid_aspect);
        rect.x = (win_w - rect.w) / 2;
        rect.y = 0;
    } else {
        rect.w = win_w;
        rect.h = (int)(win_w / vid_aspect);
        rect.x = 0;
        rect.y = (win_h - rect.h) / 2;
    }
    return rect;
}

int main(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) return -1;

    SDL_Window* window = SDL_CreateWindow(
        "FFmpeg VLC-Like Player",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1024, 600, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
    );
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    bool running = true;
    bool is_fullscreen = false;
    VideoPlayerState player;

    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);

            if (event.type == SDL_QUIT) {
                running = false;
            }
            else if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_SPACE && player.is_open) {
                    player.is_paused = !player.is_paused;
                    if (player.audio_dev > 0) SDL_PauseAudioDevice(player.audio_dev, player.is_paused ? 1 : 0);
                }
                else if (event.key.keysym.sym == SDLK_f) {
                    is_fullscreen = !is_fullscreen;
                    SDL_SetWindowFullscreen(window, is_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                }
            }
            else if (event.type == SDL_DROPFILE) {
                char* dropped_file = event.drop.file;
                OpenVideo(dropped_file, player, renderer);
                SDL_free(dropped_file);
            }
        }

        AdvancePlayback(player);

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        int win_w, win_h;
        SDL_GetWindowSize(window, &win_w, &win_h);

        // Верхнее меню
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Open File...")) {
                    std::string file = OpenFileDialog();
                    if (!file.empty()) OpenVideo(file.c_str(), player, renderer);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit")) running = false;
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        // Нижняя панель управления
        if (player.is_open) {
            ImGui::SetNextWindowPos(ImVec2(10.0f, (float)win_h - 90.0f));
            ImGui::SetNextWindowSize(ImVec2((float)win_w - 20.0f, 80.0f));
            ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

            float seek_pos = (float)player.current_time_sec;
            ImGui::PushItemWidth(-1);
            if (ImGui::SliderFloat("##time", &seek_pos, 0.0f, (float)player.duration_sec, "")) {
                SeekTo(player, seek_pos);
            }
            ImGui::PopItemWidth();

            if (ImGui::Button(player.is_paused ? " Play " : "Pause")) {
                player.is_paused = !player.is_paused;
                if (player.audio_dev > 0) SDL_PauseAudioDevice(player.audio_dev, player.is_paused ? 1 : 0);
            }

            ImGui::SameLine();
            ImGui::Text("%s / %s", FormatTime(player.current_time_sec).c_str(), FormatTime(player.duration_sec).c_str());

            ImGui::SameLine(0, 30.0f);
            ImGui::Text("Volume:");
            ImGui::SameLine();
            ImGui::PushItemWidth(120.0f);
            ImGui::SliderFloat("##vol", &player.volume, 0.0f, 1.0f, "%.0f%%");
            ImGui::PopItemWidth();

            ImGui::SameLine(0, 30.0f);
            if (ImGui::Button(is_fullscreen ? "Windowed" : "Fullscreen")) {
                is_fullscreen = !is_fullscreen;
                SDL_SetWindowFullscreen(window, is_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
            }

            ImGui::End();
        }

        ImGui::Render();

        SDL_SetRenderDrawColor(renderer, 10, 10, 10, 255);
        SDL_RenderClear(renderer);

        if (player.is_open && player.texture) {
            SDL_Rect vp = CalculateViewport(win_w, win_h, player.width, player.height);
            SDL_RenderCopy(renderer, player.texture, nullptr, &vp);
        }

        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    CloseVideo(player);
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
