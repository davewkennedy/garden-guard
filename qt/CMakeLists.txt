cmake_minimum_required(VERSION 3.16)
project(garden_plus LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia)
set(CMAKE_AUTOMOC ON)   # runs Qt's moc for classes with Q_OBJECT (signals/slots)

add_executable(garden
    src/main.cpp
    src/data.h
    src/game.h src/game.cpp
    src/renderer.h src/renderer.cpp
    src/boardwidget.h src/boardwidget.cpp
    src/audio.h src/audio.cpp
    src/mainwindow.h src/mainwindow.cpp
    src/autopilot.h src/autopilot.cpp
)
target_link_libraries(garden PRIVATE Qt6::Widgets Qt6::Multimedia)
target_compile_options(garden PRIVATE -Wall -Wextra)

# Headless simulation test (no window or sound): ./build/debug/garden_simtest
add_executable(garden_simtest tests/simtest.cpp src/game.h src/game.cpp src/autopilot.h src/autopilot.cpp)
target_link_libraries(garden_simtest PRIVATE Qt6::Core)
target_compile_options(garden_simtest PRIVATE -Wall -Wextra)
