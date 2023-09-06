cmake_minimum_required(VERSION 3.25)
project(ffmpeg_sdl2_player)
add_executable(ffmpeg_sdl2_player main.cpp)
add_executable(player player.cpp)
add_executable(screen screen.cpp)
add_executable(test test.cpp)
include_directories(${PROJECT_SOURCE_DIR}/include)
#include_directories(${PROJECT_SOURCE_DIR}/lib)

#link_directories(${PROJECT_SOURCE_DIR}/lib)
# 链接 Audio Unit 框架
target_link_libraries(test
        "-framework AudioUnit"
        "-framework CoreAudio"
        "-framework AudioToolbox"
        )
target_link_libraries(screen ${PROJECT_SOURCE_DIR}/lib/libpostproc.dylib ${PROJECT_SOURCE_DIR}/lib/libswscale.dylib
        ${PROJECT_SOURCE_DIR}/lib/libswresample.dylib ${PROJECT_SOURCE_DIR}/lib/libavformat.dylib
        ${PROJECT_SOURCE_DIR}/lib/libavcodec.dylib ${PROJECT_SOURCE_DIR}/lib/libavutil.dylib
        ${PROJECT_SOURCE_DIR}/lib/libavdevice.dylib ${PROJECT_SOURCE_DIR}/lib/libavfilter.dylib
        ${PROJECT_SOURCE_DIR}/lib/libSDL2.dylib)
target_link_libraries(test ${PROJECT_SOURCE_DIR}/lib/libpostproc.dylib ${PROJECT_SOURCE_DIR}/lib/libswscale.dylib
        ${PROJECT_SOURCE_DIR}/lib/libswresample.dylib ${PROJECT_SOURCE_DIR}/lib/libavformat.dylib
        ${PROJECT_SOURCE_DIR}/lib/libavcodec.dylib ${PROJECT_SOURCE_DIR}/lib/libavutil.dylib
        ${PROJECT_SOURCE_DIR}/lib/libavdevice.dylib ${PROJECT_SOURCE_DIR}/lib/libavfilter.dylib
        ${PROJECT_SOURCE_DIR}/lib/libSDL2.dylib)
target_link_libraries(ffmpeg_sdl2_player ${PROJECT_SOURCE_DIR}/lib/libpostproc.dylib ${PROJECT_SOURCE_DIR}/lib/libswscale.dylib
        ${PROJECT_SOURCE_DIR}/lib/libswresample.dylib ${PROJECT_SOURCE_DIR}/lib/libavformat.dylib
        ${PROJECT_SOURCE_DIR}/lib/libavcodec.dylib ${PROJECT_SOURCE_DIR}/lib/libavutil.dylib
        ${PROJECT_SOURCE_DIR}/lib/libavdevice.dylib ${PROJECT_SOURCE_DIR}/lib/libavfilter.dylib
        ${PROJECT_SOURCE_DIR}/lib/libSDL2.dylib)
target_link_libraries(player ${PROJECT_SOURCE_DIR}/lib/libpostproc.dylib ${PROJECT_SOURCE_DIR}/lib/libswscale.dylib
        ${PROJECT_SOURCE_DIR}/lib/libswresample.dylib ${PROJECT_SOURCE_DIR}/lib/libavformat.dylib
        ${PROJECT_SOURCE_DIR}/lib/libavcodec.dylib ${PROJECT_SOURCE_DIR}/lib/libavutil.dylib
        ${PROJECT_SOURCE_DIR}/lib/libavdevice.dylib ${PROJECT_SOURCE_DIR}/lib/libavfilter.dylib
        ${PROJECT_SOURCE_DIR}/lib/libSDL2.dylib)
set(CMAKE_CXX_STANDARD 17)

