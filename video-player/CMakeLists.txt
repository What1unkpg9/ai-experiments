cmake_minimum_required(VERSION 3.20)
project(VideoPlayer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Поиск пакетов
find_package(FFmpeg REQUIRED)
find_package(SDL2 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

add_executable(VideoPlayer src/main.cpp)

# Подключение заголовочных файлов FFmpeg
target_include_directories(VideoPlayer PRIVATE ${FFMPEG_INCLUDE_DIRS})

# Линковка всех найденных библиотек
target_link_libraries(VideoPlayer PRIVATE
    ${FFMPEG_LIBRARIES}
    SDL2::SDL2main
    SDL2::SDL2
    imgui::imgui
)

# Автоматическое копирование всех нужных DLL в папку с .exe
add_custom_command(TARGET VideoPlayer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_RUNTIME_DLLS:VideoPlayer>
    $<TARGET_FILE_DIR:VideoPlayer>
    COMMAND_EXPAND_LISTS
)
