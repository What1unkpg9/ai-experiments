#include <SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer.h>
#include <iostream>

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
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

    // Инициализация ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    bool running = true;
    bool show_info_modal = false;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
        }

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // Верхняя панель меню
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Open File...")) {
                    // Место для вызова системного диалога открытия файла
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

        // Всплывающее окно "Сведения"
        if (show_info_modal) {
            ImGui::Begin("Сведения о файле", &show_info_modal);
            ImGui::Text("FFmpeg Version: %s", av_version_info());
            ImGui::Separator();
            ImGui::Text("Кодек: (файл не открыт)");
            ImGui::Text("Разрешение: -");
            ImGui::End();
        }

        // Отрисовка кадра
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
        SDL_RenderPresent(renderer);
    }

    // Очистка ресурсов
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
