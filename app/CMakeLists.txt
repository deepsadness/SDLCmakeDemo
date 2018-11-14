cmake_minimum_required(VERSION 3.4.1)

#直接添加SDL库
include_directories(${CMAKE_SOURCE_DIR}/libs/SDL2/include)
add_subdirectory(${CMAKE_SOURCE_DIR}/libs/SDL2)

# 添加FFMpeg
#native-lib
set(FFMPEG_INCLUDE ${CMAKE_SOURCE_DIR}/libs/ffmpeg/include)
set(LINK_DIR ${CMAKE_SOURCE_DIR}/libs/${ANDROID_ABI})
include_directories(${FFMPEG_INCLUDE})

add_library(ffmpeg SHARED IMPORTED)
set_target_properties(ffmpeg PROPERTIES IMPORTED_LOCATION ${LINK_DIR}/libffmpeg.so)

#LOCAL_PATH := $(call my-dir)
#
#include $(CLEAR_VARS)
#
#LOCAL_MODULE := main
#
#SDL_PATH := ../SDL
#
#LOCAL_C_INCLUDES := $(LOCAL_PATH)/$(SDL_PATH)/include
#
## Add your application source files here...
#LOCAL_SRC_FILES := YourSourceHere.c
#
#LOCAL_SHARED_LIBRARIES := SDL2
#
#LOCAL_LDLIBS := -lGLESv1_CM -lGLESv2 -llog


add_library(
        #他原来的名字，就是叫main  保持一致
        main
        # Sets the library as a shared library.
        SHARED
        # Provides a relative path to your source file(s).
        src/main/cpp/native-lib-su.cpp)


find_library( # Sets the name of the path variable.
        log-lib

        # Specifies the name of the NDK library that
        # you want CMake to locate.
        log)

target_link_libraries( # Specifies the target library.
        main
        SDL2
        GLESv1_CM
        GLESv2
        ffmpeg
        # Links the target library to the log library
        # included in the NDK.
        ${log-lib})