#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>
#include <iostream>
#include <string>

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libswscale/swscale.h>
    #include <libavutil/avutil.h>
    #include <libavutil/imgutils.h>
}

struct VideoPlayerState {
    bool is_open = false;
    std::string filename = "";
    std::string codec_name = "None";
    int width = 0;
    int height = 0;
    double fps = 0.0;
    double duration_sec = 0.0;

    AVFormatContext* fmt_ctx = nullptr;
    AVCodecContext* codec_ctx = nullptr;
    int video_stream_idx = -1;
    SwsContext* sws_ctx = nullptr;

    AVFrame* av_frame = nullptr;
    AVFrame* frame_rgba = nullptr;
    uint8_t* buffer = nullptr;

    SDL_Texture* texture = nullptr;
};

// Системный диалог открытия файла Windows
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
    if (state.texture) { SDL_DestroyTexture(state.texture); state.texture = nullptr; }
    if (state.sws_ctx) { sws_freeContext(state.sws_ctx); state.sws_ctx = nullptr; }
    if (state.frame_rgba) { av_frame_free(&state.frame_rgba); }
    if (state.av_frame) { av_frame_free(&state.av_frame); }
    if (state.buffer) { av_free(state.buffer); state.buffer = nullptr; }
    if (state.codec_ctx) { avcodec_free_context(&state.codec_ctx); }
    if (state.fmt_ctx) { avformat_close_input(&state.fmt_ctx); }
    state.is_open = false;
}

bool OpenVideo(const char* filepath, VideoPlayerState& state, SDL_Renderer* renderer) {
    CloseVideo(state);

    if (avformat_open_input(&state.fmt_ctx, filepath, nullptr, nullptr) < 0) return false;
    if (avformat_find_stream_info(state.fmt_ctx, nullptr) < 0) return false;

    state.video_stream_idx = av_find_best_stream(state.fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (state.video_stream_idx < 0) return false;

    AVStream* stream = state.fmt_ctx->streams[state.video_stream_idx];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) return false;

    state.codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(state.codec_ctx, stream->codecpar);
    if (avcodec_open2(state.codec_ctx, codec, nullptr) < 0) return false;

    state.width = state.codec_ctx->width;
    state.height = state.codec_ctx->height;
    state.filename = filepath;
    state.codec_name = codec->long_name ? codec->long_name : codec->name;

    if (stream->avg_frame_rate.den > 0) {
        state.fps = av_q2d(stream->avg_frame_rate);
    }
    if (state.fmt_ctx->duration != AV_NOPTS_VALUE) {
        state.duration_sec = state.fmt_ctx->duration / (double)AV_TIME_BASE;
    }

    state.sws_ctx = sws_getContext(
        state.width, state.height, state.codec_ctx->pix_fmt,
        state.width, state.height, AV_PIX_FMT_RGBA,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );

    state.av_frame = av_frame_alloc();
    state.frame_rgba = av_frame_alloc();

    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGBA, state.width, state.height, 1);
    state.buffer = (uint8_t*)av_malloc(numBytes * sizeof(uint8_t));
    av_image_fill_arrays(state.frame_rgba->data, state.frame_rgba->linesize, state.buffer, AV_PIX_FMT_RGBA, state.width, state.height, 1);

    state.texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_STREAMING,
        state.width, state.height
    );

    state.is_open = true;
    return true;
}

void DecodeNextFrame(VideoPlayerState& state) {
    if (!state.is_open) return;

    AVPacket packet;
    while (av_read_frame(state.fmt_ctx, &packet) >= 0) {
        if (packet.stream_index == state.video_stream_idx) {
            int response = avcodec_send_packet(state.codec_ctx, &packet);
            if (response >= 0) {
                response = avcodec_receive_frame(state.codec_ctx, state.av_frame);
                if (response >= 0) {
                    sws_scale(
                        state.sws_ctx,
                        (const uint8_t* const*)state.av_frame->data,
                        state.av_frame->linesize,
                        0, state.height,
                        state.frame_rgba->data,
                        state.frame_rgba->linesize
                    );

                    SDL_UpdateTexture(
                        state.texture,
                        nullptr,
                        state.frame_rgba->data[0],
                        state.frame_rgba->linesize[0]
                    );

                    av_packet_unref(&packet);
                    break;
                }
            }
        }
        av_packet_unref(&packet);
    }
}

int main(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return -1;

    SDL_Window* window = SDL_CreateWindow(
        "FFmpeg Video Player",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1024, 600, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
    );
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    bool running = true;
    bool show_info_modal = false;
    VideoPlayerState player;

    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);

            if (event.type == SDL_QUIT) {
                running = false;
            }
            else if (event.type == SDL_DROPFILE) {
                char* dropped_file = event.drop.file;
                OpenVideo(dropped_file, player, renderer);
                SDL_free(dropped_file);
            }
        }

        // Декодируем следующий кадр
        if (player.is_open) {
            DecodeNextFrame(player);
        }

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Open File...")) {
                    std::string file = OpenFileDialog();
                    if (!file.empty()) {
                        OpenVideo(file.c_str(), player, renderer);
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit")) running = false;
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Info")) {
                if (ImGui::MenuItem("Media Details")) {
                    show_info_modal = true;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        if (show_info_modal) {
            ImGui::Begin("Media Details", &show_info_modal);
            ImGui::Text("FFmpeg Version: %s", av_version_info());
            ImGui::Separator();

            if (player.is_open) {
                ImGui::Text("File: %s", player.filename.c_str());
                ImGui::Text("Codec: %s", player.codec_name.c_str());
                ImGui::Text("Resolution: %dx%d", player.width, player.height);
                ImGui::Text("FPS: %.2f", player.fps);
                ImGui::Text("Duration: %.1f sec", player.duration_sec);
            } else {
                ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "No video opened. Use File -> Open File or Drag & Drop.");
            }

            ImGui::End();
        }

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255);
        SDL_RenderClear(renderer);

        // Отрисовка видеокадра
        if (player.is_open && player.texture) {
            SDL_RenderCopy(renderer, player.texture, nullptr, nullptr);
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
