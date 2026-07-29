#define WIN32_LEAN_AND_MEAN

// 1. Временно переименовываем конфликты WinAPI
#define Rectangle WinRectangle
#define CloseWindow WinCloseWindow
#define ShowCursor WinShowCursor
#define DrawText WinDrawText
#define PlaySound WinPlaySound

#include <windows.h>
#include <shlobj.h>
#include <commdlg.h>

// 2. Освобождаем эти имена обратно для Raylib
#undef Rectangle
#undef CloseWindow
#undef ShowCursor
#undef DrawText
#undef PlaySound

// 3. Подключаем Raylib и всё остальное
#include "raylib.h"

#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <iostream> // Добавили для вывода в консоль

struct MouseFrame {
    int x, y;
    bool leftClick;
    bool rightClick;
    int delayMs;
};

enum AppState { IDLE, RECORDING, REPLAYING };

std::string GetDocumentsPath() {
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, 0, path))) {
        return std::string(path) + "\\MouseRecorder";
    }
    return ".";
}

std::string OpenFileDialog() {
    OPENFILENAMEA ofn;
    char szFile[260] = { 0 };
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = (HWND)GetWindowHandle();
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "INI файлы (*.ini)\0*.ini\0Все файлы (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameA(&ofn)) {
        return std::string(ofn.lpstrFile);
    }
    return "";
}

void SaveToIni(const std::vector<MouseFrame>& frames) {
    std::string folder = GetDocumentsPath();
    CreateDirectoryA(folder.c_str(), NULL);
    std::string filePath = folder + "\\record_" + std::to_string((int)GetTime()) + ".ini";

    std::ofstream file(filePath);
    if (!file.is_open()) {
        std::cerr << "[ОШИБКА] Не удалось создать файл: " << filePath << std::endl;
        return;
    }

    file << "[Header]\nCount=" << frames.size() << "\n\n[Data]\n";
    for (size_t i = 0; i < frames.size(); ++i) {
        file << i << "=" << frames[i].x << "," << frames[i].y << "," 
             << (frames[i].leftClick ? 1 : 0) << "," 
             << (frames[i].rightClick ? 1 : 0) << "," 
             << frames[i].delayMs << "\n";
    }

    std::cout << "[УСПЕХ] Успешно сохранено " << frames.size() 
              << " кадров в: " << filePath << std::endl;
}

std::vector<MouseFrame> LoadFromIni(const std::string& path) {
    std::vector<MouseFrame> frames;
    std::ifstream file(path);

    if (!file.is_open()) {
        std::cerr << "[ОШИБКА] Не удалось открыть файл: " << path << std::endl;
        return frames;
    }

    std::string line;
    bool inData = false;
    while (std::getline(file, line)) {
        if (line.find("[Data]") != std::string::npos) {
            inData = true;
            continue;
        }
        if (inData && !line.empty()) {
            size_t eqPos = line.find('=');
            if (eqPos != std::string::npos) {
                std::string valStr = line.substr(eqPos + 1);
                std::stringstream ss(valStr);
                std::string item;
                MouseFrame f;
                std::getline(ss, item, ','); f.x = std::stoi(item);
                std::getline(ss, item, ','); f.y = std::stoi(item);
                std::getline(ss, item, ','); f.leftClick = (std::stoi(item) == 1);
                std::getline(ss, item, ','); f.rightClick = (std::stoi(item) == 1);
                std::getline(ss, item, ','); f.delayMs = std::stoi(item);
                frames.push_back(f);
            }
        }
    }

    std::cout << "[ИНФО] Загружено " << frames.size() << " кадров из " << path << std::endl;
    return frames;
}

int main() {
    std::cout << "=======================================" << std::endl;
    std::cout << "  Mouse Recorder — Запуск программы..." << std::endl;
    std::cout << "=======================================" << std::endl;

    const int screenWidth = 450;
    const int screenHeight = 350;
    InitWindow(screenWidth, screenHeight, "Mouse Recorder (Raylib)");
    SetTargetFPS(60);

    AppState state = IDLE;
    int repeatCount = 1;
    std::vector<MouseFrame> recordedFrames;

    while (!WindowShouldClose()) {
        // Нажатие F8 для запуск/стоп записи
        if (GetAsyncKeyState(VK_F8) & 0x8000) {
            if (state == IDLE) {
                state = RECORDING;
                recordedFrames.clear();
                std::cout << "\n[ЗАПИСЬ] Старт записи движения мыши..." << std::endl;
            } else if (state == RECORDING) {
                state = IDLE;
                std::cout << "[ЗАПИСЬ] Запись остановлена. Сохраняем..." << std::endl;
                SaveToIni(recordedFrames);
            }
            Sleep(200); // Анти-дребезг
        }

        // Запись точек
        if (state == RECORDING) {
            POINT p;
            GetCursorPos(&p);
            bool lClick = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            bool rClick = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
            recordedFrames.push_back({ (int)p.x, (int)p.y, lClick, rClick, 16 });
        }

        // GUI Raylib
        BeginDrawing();
            ClearBackground(RAYWHITE);
            DrawText("Mouse Recorder", 130, 20, 24, DARKGRAY);

            Rectangle recBtn = { 50, 70, 350, 40 };
            Color btnColor = (state == RECORDING) ? RED : LIGHTGRAY;
            DrawRectangleRec(recBtn, btnColor);
            DrawText(state == RECORDING ? "ИДЕТ ЗАПИСЬ... (Жми F8)" : "Начать запись (F8)", 80, 80, 20, DARKBLUE);

            Rectangle playBtn = { 50, 130, 350, 40 };
            DrawRectangleRec(playBtn, SKYBLUE);
            DrawText("Выбрать .ini и повторить", 90, 140, 20, DARKBLUE);

            if (CheckCollisionPointRec(GetMousePosition(), playBtn) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                std::cout << "\n[ДИАЛОГ] Открытие проводника для выбора .ini..." << std::endl;
                std::string file = OpenFileDialog();
                if (!file.empty()) {
                    auto frames = LoadFromIni(file);
                    if (!frames.empty()) {
                        std::cout << "[ПОВТОР] Старт воспроизведения (" << repeatCount << " раз)..." << std::endl;
                        for (int r = 0; r < repeatCount; ++r) {
                            std::cout << "[ПОВТОР] Проход №" << (r + 1) << std::endl;
                            for (const auto& f : frames) {
                                SetCursorPos(f.x, f.y);
                                Sleep(f.delayMs);
                            }
                        }
                        std::cout << "[ПОВТОР] Завершено!" << std::endl;
                    }
                } else {
                    std::cout << "[ДИАЛОГ] Файл не выбран." << std::endl;
                }
            }

            DrawText(TextFormat("Повторов: %d (Стрелки УВЕРХ/ВНИЗ)", repeatCount), 50, 200, 16, BLACK);
            if (IsKeyPressed(KEY_UP)) { repeatCount++; std::cout << "[НАСТРОЙКА] Повторов: " << repeatCount << std::endl; }
            if (IsKeyPressed(KEY_DOWN) && repeatCount > 1) { repeatCount--; std::cout << "[НАСТРОЙКА] Повторов: " << repeatCount << std::endl; }

            DrawText("Сохраняется в: Документы/MouseRecorder", 30, 300, 14, GRAY);
        EndDrawing();
    }

    std::cout << "\n[ВЫХОД] Закрытие программы..." << std::endl;
    CloseWindow();
    return 0;
}
