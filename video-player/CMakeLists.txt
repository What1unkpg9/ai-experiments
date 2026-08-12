cmake_minimum_required(VERSION 3.20)
project(VideoPlayer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Поиск установленных через vcpkg пакетов
find_package(FFmpeg REQUIRED)
find_package(SDL2 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

# 2. Автоматическое скачивание недостающих файлов бэкенда ImGui (SDL2 + SDLRenderer2)
set(IMGUI_BACKENDS_DIR "${CMAKE_CURRENT_BINARY_DIR}/imgui_backends")
file(MAKE_DIRECTORY ${IMGUI_BACKENDS_DIR})

set(BACKEND_FILES
    imgui_impl_sdl2.h
    imgui_impl_sdl2.cpp
    imgui_impl_sdlrenderer2.h
    imgui_impl_sdlrenderer2.cpp
)

foreach(FILE ${BACKEND_FILES})
    if(NOT EXISTS "${IMGUI_BACKENDS_DIR}/${FILE}")
        message(STATUS "Downloading ImGui backend file: ${FILE}")
        file(DOWNLOAD
            "https://raw.githubusercontent.com/ocornut/imgui/master/backends/${FILE}"
            "${IMGUI_BACKENDS_DIR}/${FILE}"
        )
    endif()
endforeach()

# 3. Создание исполняемого файла с исходниками бэкенда
add_executable(VideoPlayer
    src/main.cpp
    ${IMGUI_BACKENDS_DIR}/imgui_impl_sdl2.cpp
    ${IMGUI_BACKENDS_DIR}/imgui_impl_sdlrenderer2.cpp
)

# 4. Подключение заголовочных файлов
target_include_directories(VideoPlayer PRIVATE
    ${FFMPEG_INCLUDE_DIRS}
    ${IMGUI_BACKENDS_DIR}
)

# 5. Линковка библиотек
target_link_libraries(VideoPlayer PRIVATE
    ${FFMPEG_LIBRARIES}
    SDL2::SDL2main
    SDL2::SDL2
    imgui::imgui
)

# 6. Копирование DLL-файлов в папку с .exe
add_custom_command(TARGET VideoPlayer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_RUNTIME_DLLS:VideoPlayer>
    $<TARGET_FILE_DIR:VideoPlayer>
    COMMAND_EXPAND_LISTS
)
