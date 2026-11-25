cmake_minimum_required(VERSION 3.16)
project(garden_plusplus LANGUAGES CXX)

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
    src/immersive.h src/immersive.cpp
    src/audio.h src/audio.cpp
    src/music.h src/music.cpp
    src/mainwindow.h src/mainwindow.cpp src/focusfire.cpp src/journal.cpp src/difficulty.cpp
    src/autopilot.h src/autopilot.cpp
)
target_link_libraries(garden PRIVATE Qt6::Widgets Qt6::Multimedia)
target_compile_options(garden PRIVATE -Wall -Wextra)

# Music: the SAME files the web ships (web/audio, Dale's), embedded as :/audio/...
# Re-run cmake after adding or renaming files there. Missing files = silent music.
set(GG_AUDIO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../web/audio)
file(GLOB GG_AUDIO_FILES CONFIGURE_DEPENDS ${GG_AUDIO_DIR}/*.mp3 ${GG_AUDIO_DIR}/music.json)
if(GG_AUDIO_FILES)
    qt_add_resources(garden "audio" PREFIX "/audio" BASE ${GG_AUDIO_DIR} FILES ${GG_AUDIO_FILES})
else()
    message(WARNING "web/audio has no music files: the Qt build has no music")
endif()

# Headless simulation test (no window or sound): ./build/debug/garden_simtest
add_executable(garden_simtest tests/simtest.cpp src/game.h src/game.cpp src/autopilot.h src/autopilot.cpp)
target_link_libraries(garden_simtest PRIVATE Qt6::Core)
target_compile_options(garden_simtest PRIVATE -Wall -Wextra)

# Headless music test (no window, no audio device): ./build/debug/garden_musictest
# Loop seam, crossfade law, duck/stinger state machine, and decoding of the real files.
add_executable(garden_musictest tests/musictest.cpp src/music.h src/music.cpp src/game.h src/game.cpp)
target_link_libraries(garden_musictest PRIVATE Qt6::Core Qt6::Multimedia)
target_compile_options(garden_musictest PRIVATE -Wall -Wextra)
if(GG_AUDIO_FILES)
    qt_add_resources(garden_musictest "audio_t" PREFIX "/audio" BASE ${GG_AUDIO_DIR} FILES ${GG_AUDIO_FILES})
endif()
