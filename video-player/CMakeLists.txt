cmake_minimum_required(VERSION 3.20)
project(VideoPlayer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Поиск необходимых библиотек через vcpkg
find_package(FFmpeg REQUIRED COMPONENTS AVCODEC AVFORMAT SWSCALE AVUTIL)
find_package(SDL2 REQUIRED)
find_package(imgui REQUIRED)

add_executable(VideoPlayer src/main.cpp)

target_link_libraries(VideoPlayer PRIVATE
    FFmpeg::AVCODEC
    FFmpeg::AVFORMAT
    FFmpeg::SWSCALE
    FFmpeg::AVUTIL
    SDL2::SDL2main
    SDL2::SDL2
    imgui::imgui
)

# Автоматическое копирование DLL в папку с .exe при сборке
add_custom_command(TARGET VideoPlayer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_RUNTIME_DLLS:VideoPlayer>
    $<TARGET_FILE_DIR:VideoPlayer>
    COMMAND_EXPAND_LISTS
)
