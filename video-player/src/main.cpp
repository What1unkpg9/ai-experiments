#include <SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>
#include <iostream>
#include <string>

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libavutil/avutil.h>
}

struct MediaInfo {
    bool loaded = false;
    std::string filename = "";
    std::string codec_name = "Неизвестно";
    int width = 0;
    int height = 0;
    double duration_sec = 0.0;
    double fps = 0.0;
};

// Функция чтения метаданных через FFmpeg
bool LoadMediaMetadata(const char* filepath, MediaInfo& info) {
    AVFormatContext* fmt_ctx = nullptr;

    if (avformat_open_input(&fmt_ctx, filepath, nullptr, nullptr) < 0) {
        return false;
    }

    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        avformat_close_input(&fmt_ctx);
        return false;
    }

    int video_stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_stream_idx >= 0) {
        AVStream* stream = fmt_ctx->streams[video_stream_idx];
        const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);

        info.filename = filepath;
        info.codec_name = codec ? codec->long_name : "Неизвестно";
        info.width = stream->codecpar->width;
        info.height = stream->codecpar->height;
        
        if (stream->duration != AV_NOPTS_VALUE) {
            info.duration_sec = stream->duration * av_q2d(stream->time_base);
        } else if (fmt_ctx->duration != AV_NOPTS_VALUE) {
            info.duration_sec = fmt_ctx->duration / (double)AV_TIME_BASE;
        }

        if (stream->avg_frame_rate.den > 0) {
            info.fps = av_q2d(stream->avg_frame_rate);
        }

        info.loaded = true;
    }

    avformat_close_input(&fmt_ctx);
    return info.loaded;
}

int main(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        return -1;
    }

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
    MediaInfo current_media;

    // Включаем поддержку Drag-and-Drop
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            
            if (event.type == SDL_QUIT) {
                running = false;
            }
            // Обработка перетаскивания файла в окно
            else if (event.type == SDL_DROPFILE) {
                char* dropped_file = event.drop.file;
                if (LoadMediaMetadata(dropped_file, current_media)) {
                    show_info_modal = true;
                }
                SDL_free(dropped_file);
            }
        }

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
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

        // Всплывающее окно "Сведения"
        if (show_info_modal) {
            ImGui::Begin("Сведения о файле", &show_info_modal);
            ImGui::Text("FFmpeg Version: %s", av_version_info());
            ImGui::Separator();
            
            if (current_media.loaded) {
                ImGui::Text("Файл: %s", current_media.filename.c_str());
                ImGui::Text("Кодек: %s", current_media.codec_name.c_str());
                ImGui::Text("Разрешение: %dx%d", current_media.width, current_media.height);
                ImGui::Text("FPS: %.2f", current_media.fps);
                ImGui::Text("Длительность: %.1f сек", current_media.duration_sec);
            } else {
                ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "Перетащите видеофайл в окно программы!");
            }
            
            ImGui::End();
        }

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255);
        SDL_RenderClear(renderer);
        
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
