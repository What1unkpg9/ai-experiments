cmake_minimum_required(VERSION 3.20)
project(MouseRecorder CXX)

# Совместимость со старыми CMakeLists сторонних библиотек (Raylib)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Скачиваем Raylib 5.0
include(FetchContent)
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.0
)
FetchContent_MakeAvailable(raylib)

# Исходники
add_executable(MouseRecorder src/main.cpp)

# Подключаем библиотеки
target_link_libraries(MouseRecorder PRIVATE raylib winmm comdlg32 user32 shell32)
