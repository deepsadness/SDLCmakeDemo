//
// Created by Cry on 2018/11/13.
//

#ifndef SDLCMAKEDEMO_LOG_H
#define SDLCMAKEDEMO_LOG_H

#ifdef __ANDROID__
#include <jni.h>
#include <android/log.h>
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR,"ERROR: ", __VA_ARGS__)
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO,"INFO: ", __VA_ARGS__)
#else
#define ALOGE(format, ...) printf("ERROR: " format "\n",##__VA_ARGS__)
#define ALOGI(format, ...) printf("INFO: " format "\n",##__VA_ARGS__)
#endif //__ANDROID__

#endif //SDLCMAKEDEMO_LOG_H
