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
#include <vector>
#include <cmath>

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libswscale/swscale.h>
    #include <libswresample/swresample.h>
    #include <libavutil/avutil.h>
    #include <libavutil/imgutils.h>
    #include <libavutil/channel_layout.h>
    #include <libavutil/pixdesc.h>
}

enum AspectRatioMode {
    ASPECT_AUTO = 0,
    ASPECT_16_9,
    ASPECT_4_3,
    ASPECT_21_9,
    ASPECT_STRETCH
};

enum LoopMode {
    LOOP_OFF = 0,
    LOOP_ONE,
    LOOP_ALL
};

struct MediaDetails {
    std::string container_name = "-";
    int64_t bit_rate = 0;
    std::string v_codec = "-";
    int width = 0;
    int height = 0;
    double fps = 0.0;
    std::string pix_fmt = "-";
    std::string a_codec = "-";
    int sample_rate = 0;
    int channels = 0;
    std::string sample_fmt = "-";
};

struct OSDMessage {
    std::string text = "";
    Uint32 expire_ticks = 0;
};

struct VideoPlayerState {
    bool is_open = false;
    bool is_paused = false;
    bool is_muted = false;
    bool frame_step_requested = false;
    
    std::string filepath = "";
    double duration_sec = 0.0;
    double current_time_sec = 0.0;
    float playback_speed = 1.0f;
    float volume = 0.6f; // До 2.0 (200%)

    AspectRatioMode aspect_mode = ASPECT_AUTO;
    LoopMode loop_mode = LOOP_OFF;

    // A-B Loop
    bool ab_loop_enabled = false;
    double ab_point_a = -1.0;
    double ab_point_b = -1.0;

    // Audio Visualizer data
    float audio_peak_level = 0.0f;

    MediaDetails details;
    OSDMessage osd;

    // FFmpeg Video
    AVFormatContext* fmt_ctx = nullptr;
    AVCodecContext* video_codec_ctx = nullptr;
    int video_stream_idx = -1;
    SwsContext* sws_ctx = nullptr;
    AVFrame* av_frame = nullptr;
    AVFrame* frame_rgba = nullptr;
    uint8_t* video_buffer = nullptr;
    SDL_Texture* texture = nullptr;

    // FFmpeg Audio
    AVCodecContext* audio_codec_ctx = nullptr;
    int audio_stream_idx = -1;
    SwrContext* swr_ctx = nullptr;
    SDL_AudioDeviceID audio_dev = 0;
    AVFrame* audio_frame = nullptr;

    Uint64 last_frame_ticks = 0;
};

void ShowOSD(VideoPlayerState& state, const std::string& text, Uint32 duration_ms = 2200) {
    state.osd.text = text;
    state.osd.expire_ticks = SDL_GetTicks() + duration_ms;
}

std::string FormatTime(double seconds) {
    int total = (int)seconds;
    int h = total / 3600;
    int m = (total % 3600) / 60;
    int s = total % 60;
    char buf[64];
    if (h > 0)
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
    else
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
    ofn.lpstrFilter = "Media Files\0*.mp4;*.mkv;*.avi;*.mov;*.wmv;*.flv;*.webm;*.m4v;*.mp3;*.wav\0All Files\0*.*\0";
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
    state.ab_loop_enabled = false;
    state.ab_point_a = -1.0;
    state.ab_point_b = -1.0;
    state.details = MediaDetails();
}

bool OpenVideo(const char* filepath, VideoPlayerState& state, SDL_Renderer* renderer) {
    CloseVideo(state);

    if (avformat_open_input(&state.fmt_ctx, filepath, nullptr, nullptr) < 0) return false;
    if (avformat_find_stream_info(state.fmt_ctx, nullptr) < 0) return false;

    state.filepath = filepath;
    state.details.container_name = state.fmt_ctx->iformat->long_name ? state.fmt_ctx->iformat->long_name : state.fmt_ctx->iformat->name;
    state.details.bit_rate = state.fmt_ctx->bit_rate;

    if (state.fmt_ctx->duration != AV_NOPTS_VALUE) {
        state.duration_sec = state.fmt_ctx->duration / (double)AV_TIME_BASE;
    }

    state.video_stream_idx = av_find_best_stream(state.fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (state.video_stream_idx >= 0) {
        AVStream* stream = state.fmt_ctx->streams[state.video_stream_idx];
        const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (codec) {
            state.video_codec_ctx = avcodec_alloc_context3(codec);
            avcodec_parameters_to_context(state.video_codec_ctx, stream->codecpar);
            if (avcodec_open2(state.video_codec_ctx, codec, nullptr) >= 0) {
                state.details.v_codec = codec->long_name ? codec->long_name : codec->name;
                state.details.width = state.video_codec_ctx->width;
                state.details.height = state.video_codec_ctx->height;
                
                const char* pix_fmt_name = av_get_pix_fmt_name(state.video_codec_ctx->pix_fmt);
                state.details.pix_fmt = pix_fmt_name ? pix_fmt_name : "Unknown";

                if (stream->avg_frame_rate.den > 0) {
                    state.details.fps = av_q2d(stream->avg_frame_rate);
                }

                state.sws_ctx = sws_getContext(
                    state.details.width, state.details.height, state.video_codec_ctx->pix_fmt,
                    state.details.width, state.details.height, AV_PIX_FMT_RGBA,
                    SWS_BICUBIC | SWS_ACCURATE_RND, nullptr, nullptr, nullptr
                );

                state.av_frame = av_frame_alloc();
                state.frame_rgba = av_frame_alloc();

                int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGBA, state.details.width, state.details.height, 1);
                state.video_buffer = (uint8_t*)av_malloc(numBytes * sizeof(uint8_t));
                av_image_fill_arrays(state.frame_rgba->data, state.frame_rgba->linesize, state.video_buffer, AV_PIX_FMT_RGBA, state.details.width, state.details.height, 1);

                state.texture = SDL_CreateTexture(
                    renderer, SDL_PIXELFORMAT_RGBA32,
                    SDL_TEXTUREACCESS_STREAMING, state.details.width, state.details.height
                );
            }
        }
    }

    state.audio_stream_idx = av_find_best_stream(state.fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (state.audio_stream_idx >= 0) {
        AVStream* astream = state.fmt_ctx->streams[state.audio_stream_idx];
        const AVCodec* acodec = avcodec_find_decoder(astream->codecpar->codec_id);
        if (acodec) {
            state.audio_codec_ctx = avcodec_alloc_context3(acodec);
            avcodec_parameters_to_context(state.audio_codec_ctx, astream->codecpar);
            if (avcodec_open2(state.audio_codec_ctx, acodec, nullptr) >= 0) {
                state.details.a_codec = acodec->long_name ? acodec->long_name : acodec->name;
                state.details.sample_rate = state.audio_codec_ctx->sample_rate;
                state.details.channels = state.audio_codec_ctx->ch_layout.nb_channels;
                
                const char* sfmt_name = av_get_sample_fmt_name(state.audio_codec_ctx->sample_fmt);
                state.details.sample_fmt = sfmt_name ? sfmt_name : "Unknown";

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

    state.is_open = (state.video_stream_idx >= 0 || state.audio_stream_idx >= 0);
    state.last_frame_ticks = SDL_GetTicks64();

    std::string filename_only = filepath;
    size_t last_slash = filename_only.find_last_of("\\/");
    if (last_slash != std::string::npos) filename_only = filename_only.substr(last_slash + 1);
    ShowOSD(state, "Opened: " + filename_only);

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

void SaveSnapshot(VideoPlayerState& state) {
    if (!state.is_open || !state.frame_rgba || !state.video_buffer) {
        ShowOSD(state, "Snapshot Failed: No Video");
        return;
    }

    SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(
        state.frame_rgba->data[0],
        state.details.width,
        state.details.height,
        32,
        state.frame_rgba->linesize[0],
        0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000
    );

    if (surface) {
        std::string fname = "snapshot_" + std::to_string(SDL_GetTicks()) + ".bmp";
        SDL_SaveBMP(surface, fname.c_str());
        SDL_FreeSurface(surface);
        ShowOSD(state, "Saved Snapshot: " + fname);
    }
}

void AdvancePlayback(VideoPlayerState& state) {
    if (!state.is_open) return;
    if (state.is_paused && !state.frame_step_requested) return;

    Uint64 current_ticks = SDL_GetTicks64();
    double base_fps = (state.details.fps > 0) ? state.details.fps : 25.0;
    double frame_delay = (1000.0 / (base_fps * state.playback_speed));
    bool need_video = state.frame_step_requested || ((current_ticks - state.last_frame_ticks) >= frame_delay);

    // A-B Loop Processing
    if (state.ab_loop_enabled && state.ab_point_b > state.ab_point_a && state.current_time_sec >= state.ab_point_b) {
        SeekTo(state, state.ab_point_a);
        return;
    }

    AVPacket packet;
    int read_res = av_read_frame(state.fmt_ctx, &packet);

    if (read_res < 0) { // Конец файла
        if (state.loop_mode == LOOP_ONE) {
            SeekTo(state, 0.0);
            return;
        } else {
            state.is_paused = true;
            return;
        }
    }

    if (packet.stream_index == state.video_stream_idx && need_video) {
        if (avcodec_send_packet(state.video_codec_ctx, &packet) >= 0) {
            if (avcodec_receive_frame(state.video_codec_ctx, state.av_frame) >= 0) {
                sws_scale(
                    state.sws_ctx,
                    (const uint8_t* const*)state.av_frame->data,
                    state.av_frame->linesize,
                    0, state.details.height,
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

                if (state.frame_step_requested) {
                    state.frame_step_requested = false;
                    state.is_paused = true;
                }
            }
        }
    }
    else if (packet.stream_index == state.audio_stream_idx && state.audio_dev > 0 && !state.is_paused) {
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

                        float cur_vol = state.is_muted ? 0.0f : state.volume;
                        float peak = 0.0f;

                        for (int i = 0; i < total_samples; ++i) {
                            int val = (int)(samples[i] * cur_vol);
                            // Soft limiter для предотвращения дикого хрипа при 200%
                            if (val > 32767) val = 32767;
                            if (val < -32768) val = -32768;
                            samples[i] = (int16_t)val;

                            float abs_sample = std::abs(val) / 32768.0f;
                            if (abs_sample > peak) peak = abs_sample;
                        }

                        state.audio_peak_level = peak;
                        SDL_QueueAudio(state.audio_dev, out_buf, data_size);
                    }
                    if (out_buf) av_freep(&out_buf);
                }
            }
        }
    }

    av_packet_unref(&packet);
}

SDL_Rect CalculateViewport(int win_w, int win_h, int vid_w, int vid_h, AspectRatioMode mode) {
    if (vid_w <= 0 || vid_h <= 0) return { 0, 0, win_w, win_h };
    if (mode == ASPECT_STRETCH) return { 0, 0, win_w, win_h };

    float vid_aspect = (float)vid_w / (float)vid_h;

    switch (mode) {
        case ASPECT_16_9: vid_aspect = 16.0f / 9.0f; break;
        case ASPECT_4_3:  vid_aspect = 4.0f / 3.0f; break;
        case ASPECT_21_9: vid_aspect = 21.0f / 9.0f; break;
        default: break;
    }

    float win_aspect = (float)win_w / (float)win_h;
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

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");

    SDL_Window* window = SDL_CreateWindow(
        "VLC Master Player Pro",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1150, 680, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
    );
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark();

    style.WindowRounding = 8.0f;
    style.FrameRounding = 5.0f;
    style.PopupRounding = 5.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.08f, 0.11f, 0.88f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.22f, 0.28f, 0.40f, 0.85f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.20f, 0.26f, 0.38f, 1.00f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.38f, 0.55f, 1.00f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.35f, 0.48f, 0.72f, 1.00f);

    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    bool running = true;
    bool is_fullscreen = false;
    bool show_media_info = false;
    bool show_jump_modal = false;
    Uint32 last_mouse_motion = SDL_GetTicks();

    VideoPlayerState player;
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);

            if (event.type == SDL_QUIT) {
                running = false;
            }
            else if (event.type == SDL_MOUSEMOTION) {
                last_mouse_motion = SDL_GetTicks();
            }
            else if (event.type == SDL_MOUSEWHEEL) {
                last_mouse_motion = SDL_GetTicks();
                if (SDL_GetModState() & KMOD_SHIFT) {
                    if (event.wheel.y > 0) SeekTo(player, player.current_time_sec + 10.0);
                    else if (event.wheel.y < 0) SeekTo(player, player.current_time_sec - 10.0);
                } else {
                    if (event.wheel.y > 0) {
                        player.volume = (std::min)(2.0f, player.volume + 0.05f);
                        ShowOSD(player, "Volume: " + std::to_string((int)(player.volume * 100)) + "%");
                    } else if (event.wheel.y < 0) {
                        player.volume = (std::max)(0.0f, player.volume - 0.05f);
                        ShowOSD(player, "Volume: " + std::to_string((int)(player.volume * 100)) + "%");
                    }
                }
            }
            else if (event.type == SDL_MOUSEBUTTONDOWN) {
                last_mouse_motion = SDL_GetTicks();
                if (event.button.button == SDL_BUTTON_LEFT && event.button.clicks == 2) {
                    is_fullscreen = !is_fullscreen;
                    SDL_SetWindowFullscreen(window, is_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                }
            }
            else if (event.type == SDL_KEYDOWN) {
                last_mouse_motion = SDL_GetTicks();
                switch (event.key.keysym.sym) {
                    case SDLK_SPACE:
                        if (player.is_open) {
                            player.is_paused = !player.is_paused;
                            ShowOSD(player, player.is_paused ? "Paused" : "Playing");
                            if (player.audio_dev > 0) SDL_PauseAudioDevice(player.audio_dev, player.is_paused ? 1 : 0);
                        }
                        break;
                    case SDLK_e:
                        if (player.is_open) {
                            player.frame_step_requested = true;
                            player.is_paused = false;
                            ShowOSD(player, "Frame Step");
                        }
                        break;
                    case SDLK_f:
                        is_fullscreen = !is_fullscreen;
                        SDL_SetWindowFullscreen(window, is_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                        break;
                    case SDLK_m:
                        player.is_muted = !player.is_muted;
                        ShowOSD(player, player.is_muted ? "Mute On" : "Mute Off");
                        break;
                    case SDLK_LEFT:
                        if (player.is_open) {
                            SeekTo(player, (std::max)(0.0, player.current_time_sec - 5.0));
                            ShowOSD(player, "-5 sec");
                        }
                        break;
                    case SDLK_RIGHT:
                        if (player.is_open) {
                            SeekTo(player, (std::min)(player.duration_sec, player.current_time_sec + 5.0));
                            ShowOSD(player, "+5 sec");
                        }
                        break;
                    case SDLK_UP:
                        player.volume = (std::min)(2.0f, player.volume + 0.1f);
                        ShowOSD(player, "Volume: " + std::to_string((int)(player.volume * 100)) + "%");
                        break;
                    case SDLK_DOWN:
                        player.volume = (std::max)(0.0f, player.volume - 0.1f);
                        ShowOSD(player, "Volume: " + std::to_string((int)(player.volume * 100)) + "%");
                        break;
                    case SDLK_s:
                        if (SDL_GetModState() & KMOD_SHIFT) {
                            SaveSnapshot(player);
                        }
                        break;
                    case SDLK_j:
                        if (SDL_GetModState() & KMOD_CTRL) {
                            show_jump_modal = true;
                        }
                        break;
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

        bool ui_visible = (!player.is_open || player.is_paused || (SDL_GetTicks() - last_mouse_motion < 3000));

        if (ui_visible) {
            // Главное меню
            if (ImGui::BeginMainMenuBar()) {
                if (ImGui::BeginMenu("Media")) {
                    if (ImGui::MenuItem("Open File...", "Ctrl+O")) {
                        std::string file = OpenFileDialog();
                        if (!file.empty()) OpenVideo(file.c_str(), player, renderer);
                    }
                    if (ImGui::MenuItem("Take Snapshot", "Shift+S", false, player.is_open)) {
                        SaveSnapshot(player);
                    }
                    if (ImGui::MenuItem("Close Media", nullptr, false, player.is_open)) {
                        CloseVideo(player);
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Quit", "Alt+F4")) running = false;
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Playback")) {
                    if (ImGui::MenuItem(player.is_paused ? "Play" : "Pause", "Space", false, player.is_open)) {
                        player.is_paused = !player.is_paused;
                        if (player.audio_dev > 0) SDL_PauseAudioDevice(player.audio_dev, player.is_paused ? 1 : 0);
                    }
                    if (ImGui::MenuItem("Next Frame", "E", false, player.is_open)) {
                        player.frame_step_requested = true;
                        player.is_paused = false;
                    }
                    if (ImGui::MenuItem("Jump to Time...", "Ctrl+J", false, player.is_open)) {
                        show_jump_modal = true;
                    }
                    ImGui::Separator();
                    if (ImGui::BeginMenu("Speed")) {
                        float speeds[] = { 0.25f, 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f, 3.0f };
                        for (float spd : speeds) {
                            char label[32];
                            snprintf(label, sizeof(label), "%.2fx", spd);
                            if (ImGui::MenuItem(label, nullptr, player.playback_speed == spd)) {
                                player.playback_speed = spd;
                                ShowOSD(player, "Speed: " + std::string(label));
                            }
                        }
                        ImGui::EndMenu();
                    }
                    if (ImGui::BeginMenu("Repeat")) {
                        if (ImGui::MenuItem("Off", nullptr, player.loop_mode == LOOP_OFF)) player.loop_mode = LOOP_OFF;
                        if (ImGui::MenuItem("Repeat One File", nullptr, player.loop_mode == LOOP_ONE)) player.loop_mode = LOOP_ONE;
                        ImGui::EndMenu();
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Video")) {
                    if (ImGui::BeginMenu("Aspect Ratio")) {
                        if (ImGui::MenuItem("Auto (Default)", nullptr, player.aspect_mode == ASPECT_AUTO)) player.aspect_mode = ASPECT_AUTO;
                        if (ImGui::MenuItem("16:9", nullptr, player.aspect_mode == ASPECT_16_9)) player.aspect_mode = ASPECT_16_9;
                        if (ImGui::MenuItem("4:3", nullptr, player.aspect_mode == ASPECT_4_3)) player.aspect_mode = ASPECT_4_3;
                        if (ImGui::MenuItem("21:9", nullptr, player.aspect_mode == ASPECT_21_9)) player.aspect_mode = ASPECT_21_9;
                        if (ImGui::MenuItem("Stretch Fill", nullptr, player.aspect_mode == ASPECT_STRETCH)) player.aspect_mode = ASPECT_STRETCH;
                        ImGui::EndMenu();
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Tools")) {
                    if (ImGui::MenuItem("Media Information", "Ctrl+I", &show_media_info, player.is_open)) {}
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("View")) {
                    if (ImGui::MenuItem("Fullscreen", "F", is_fullscreen)) {
                        is_fullscreen = !is_fullscreen;
                        SDL_SetWindowFullscreen(window, is_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMainMenuBar();
            }

            // Нижняя супер-панель управления
            if (player.is_open) {
                ImGui::SetNextWindowPos(ImVec2(10.0f, (float)win_h - 95.0f));
                ImGui::SetNextWindowSize(ImVec2((float)win_w - 20.0f, 85.0f));
                ImGui::Begin("MasterControls", nullptr, 
                    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

                // Временная шкала
                float seek_pos = (float)player.current_time_sec;
                ImGui::PushItemWidth(-1);
                if (ImGui::SliderFloat("##time", &seek_pos, 0.0f, (float)player.duration_sec, "")) {
                    SeekTo(player, seek_pos);
                }
                ImGui::PopItemWidth();

                // Кнопки управления
                if (ImGui::Button(player.is_paused ? "  Play  " : " Pause ")) {
                    player.is_paused = !player.is_paused;
                    if (player.audio_dev > 0) SDL_PauseAudioDevice(player.audio_dev, player.is_paused ? 1 : 0);
                }
                ImGui::SameLine();
                if (ImGui::Button("Stop")) {
                    SeekTo(player, 0);
                    player.is_paused = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Step >")) {
                    player.frame_step_requested = true;
                    player.is_paused = false;
                }
                ImGui::SameLine();
                if (ImGui::Button("Snap 📸")) {
                    SaveSnapshot(player);
                }

                // A-B Loop кнопки
                ImGui::SameLine(0, 15.0f);
                if (ImGui::Button(player.ab_point_a >= 0 ? "[A]" : " A ")) {
                    player.ab_point_a = player.current_time_sec;
                    ShowOSD(player, "A-B: Set Point A");
                }
                ImGui::SameLine();
                if (ImGui::Button(player.ab_point_b >= 0 ? "[B]" : " B ")) {
                    player.ab_point_b = player.current_time_sec;
                    if (player.ab_point_b > player.ab_point_a) {
                        player.ab_loop_enabled = true;
                        ShowOSD(player, "A-B Loop Active");
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Clear AB")) {
                    player.ab_loop_enabled = false;
                    player.ab_point_a = -1.0;
                    player.ab_point_b = -1.0;
                    ShowOSD(player, "A-B Loop Cleared");
                }

                ImGui::SameLine(0, 15.0f);
                ImGui::Text("%s / %s", FormatTime(player.current_time_sec).c_str(), FormatTime(player.duration_sec).c_str());

                // Mute, Громкость до 200% и VU-метр
                ImGui::SameLine(0, 20.0f);
                if (ImGui::Button(player.is_muted ? "Unmute" : " Mute ")) {
                    player.is_muted = !player.is_muted;
                }
                ImGui::SameLine();
                ImGui::PushItemWidth(110.0f);
                if (player.volume > 1.0f) {
                    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
                }
                ImGui::SliderFloat("##vol", &player.volume, 0.0f, 2.0f, "%.0f%%");
                if (player.volume > 1.0f) {
                    ImGui::PopStyleColor();
                }
                ImGui::PopItemWidth();

                // Простой VU Meter индикатор
                ImGui::SameLine();
                ImGui::ProgressBar(player.audio_peak_level, ImVec2(35.0f, 0.0f), "");

                ImGui::End();
            }
        }

        // Окно ввода времени (Jump to time)
        if (show_jump_modal) {
            ImGui::OpenPopup("Jump To Time");
            show_jump_modal = false;
        }

        if (ImGui::BeginPopupModal("Jump To Time", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            static int jump_sec = 0;
            ImGui::InputInt("Target Seconds", &jump_sec);
            if (ImGui::Button("Jump", ImVec2(120, 0))) {
                SeekTo(player, (double)jump_sec);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // Окно сведений о медиафайле
        if (show_media_info && player.is_open) {
            ImGui::SetNextWindowSize(ImVec2(520, 380), ImGuiCond_FirstUseEver);
            ImGui::Begin("Media Information", &show_media_info);

            if (ImGui::BeginTabBar("InfoTabs")) {
                if (ImGui::BeginTabItem("General")) {
                    ImGui::Text("File Path: %s", player.filepath.c_str());
                    ImGui::Text("Container Format: %s", player.details.container_name.c_str());
                    ImGui::Text("Bitrate: %lld kb/s", player.details.bit_rate / 1000);
                    ImGui::Text("Duration: %s", FormatTime(player.duration_sec).c_str());
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Video Stream")) {
                    ImGui::Text("Codec: %s", player.details.v_codec.c_str());
                    ImGui::Text("Resolution: %d x %d", player.details.width, player.details.height);
                    ImGui::Text("Frame Rate: %.2f FPS", player.details.fps);
                    ImGui::Text("Pixel Format: %s", player.details.pix_fmt.c_str());
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Audio Stream")) {
                    ImGui::Text("Codec: %s", player.details.a_codec.c_str());
                    ImGui::Text("Sample Rate: %d Hz", player.details.sample_rate);
                    ImGui::Text("Channels: %d", player.details.channels);
                    ImGui::Text("Sample Format: %s", player.details.sample_fmt.c_str());
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }

            ImGui::End();
        }

        // Отрисовка OSD (Наэкранного меню)
        if (SDL_GetTicks() < player.osd.expire_ticks && !player.osd.text.empty()) {
            ImGui::SetNextWindowPos(ImVec2((float)win_w - 220.0f, 40.0f));
            ImGui::SetNextWindowSize(ImVec2(200.0f, 0.0f));
            ImGui::Begin("OSDOverlay", nullptr, 
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", player.osd.text.c_str());
            ImGui::End();
        }

        ImGui::Render();

        SDL_SetRenderDrawColor(renderer, 5, 5, 5, 255);
        SDL_RenderClear(renderer);

        if (player.is_open && player.texture) {
            SDL_Rect vp = CalculateViewport(win_w, win_h, player.details.width, player.details.height, player.aspect_mode);
            SDL_RenderCopy(renderer, player.texture, nullptr, &vp);
        }

        if (ui_visible) {
            ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        }

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
