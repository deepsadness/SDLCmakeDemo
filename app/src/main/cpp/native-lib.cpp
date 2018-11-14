#include "SDL.h"
#include "log.h"

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/avutil.h"
#include "libavutil/imgutils.h"
#include "libswscale/swscale.h"
}


/*

//Refresh Event
#define SFM_REFRESH_EVENT  (SDL_USEREVENT + 1)

#define SFM_BREAK_EVENT  (SDL_USEREVENT + 2)

int thread_exit = 0;
int thread_pause = 0;

int sfp_refresh_thread(void *opaque) {
    thread_exit = 0;
    thread_pause = 0;

    while (!thread_exit) {
        if (!thread_pause) {
            SDL_Event event;
            event.type = SFM_REFRESH_EVENT;
            SDL_PushEvent(&event);
        }
        SDL_Delay(40);
    }
    thread_exit = 0;
    thread_pause = 0;
    //Break
    SDL_Event event;
    event.type = SFM_BREAK_EVENT;
    SDL_PushEvent(&event);

    return 0;
}

extern "C"
int avError(int error) {
    char buf[1000000];
    av_strerror(error, buf, sizeof(buf));
    ALOGE("发送异常: %s", buf);
    return error;
}

extern "C"
void syslog_print(void *ptr, int level, const char *fmt, va_list vl) {
    switch (level) {
        case AV_LOG_DEBUG:
        case AV_LOG_VERBOSE:
        case AV_LOG_INFO:
        case AV_LOG_WARNING:
            __android_log_vprint(ANDROID_LOG_INFO, "ZZX", fmt, vl);
//            ALOGI(fmt, vl);
            break;
        case AV_LOG_ERROR:
            __android_log_vprint(ANDROID_LOG_ERROR, "ZZX", fmt, vl);
//            ALOGE(fmt, vl);
            break;
    }
}

extern "C"
//这里是直接定义了SDL的main方法吗
int main(int argc, char *argv[]) {

    // 打印ffmpeg信息
    const char *str = avcodec_configuration();
    ALOGI("avcodec_configuration: %s", str);

    char *video_path = argv[1];
    ALOGI("video_path  : %s", video_path);

    //开始ffmpeg注册的流程
    int ret = 0;

    //重定向log
    av_log_set_callback(syslog_print);

    //注册
    av_register_all();
    avcodec_register_all();


    //创建avformat
    AVFormatContext *pFormatCtx = avformat_alloc_context();
    ret = avformat_open_input(&pFormatCtx, video_path, NULL, NULL);

    if (ret < 0) {
        ALOGE("avformat open input failed!");
        return avError(ret);
    }

    //输出avformat
    av_dump_format(pFormatCtx, -1, video_path, 0);

    // 先去找到video_stream,然后在找AVCodec
    //先检查一边
    ret = avformat_find_stream_info(pFormatCtx, NULL);
    if (ret < 0) {
        ALOGE("Can not find Stream info!!!");
        return avError(ret);
    }

    int video_stream = -1;
    //这里就是简单的直接去找视频流
    for (int i = 0; i < pFormatCtx->nb_streams; ++i) {
        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream = i;
            break;
        }
    }

    if (video_stream == -1) {
        ALOGE("Can not find video stream!!!");
        return -1;
    }

    ALOGI("find video stream ,index = %d", video_stream);
    //创建AVCodecCtx
    //需要先去获得AVCodec
    AVCodec *pCodec = avcodec_find_decoder(pFormatCtx->streams[video_stream]->codecpar->codec_id);
    if (pCodec == NULL) {
        ALOGE("Can not find video decoder!!!");
        return -1;
    }
    //成功获取上下文。获取之后，需要对上下文的部分内容进行初始化
    AVCodecContext *pCodecCtx = avcodec_alloc_context3(pCodec);

    //将解码器的参数复制过去
    AVCodecParameters *codecParameters = pFormatCtx->streams[video_stream]->codecpar;
    ret = avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[video_stream]->codecpar);
    if (ret < 0) {
        ALOGE("avcodec_parameters_from_context error!!");
        return avError(ret);
    }

    AVDictionaryEntry *t = NULL;
    while ((t = av_dict_get(pFormatCtx->metadata, "", t, AV_DICT_IGNORE_SUFFIX))) {
        char *key = t->key;
        char *value = t->value;
        ALOGI("key = %s,value = %s", key, value);
    }

    int height = codecParameters->height;
    int width = codecParameters->width;
    ALOGI("width = %d,height = %d", width, height);

    //完成初始化的参数之后，就要打开解码器，准备解码啦！！
    ret = avcodec_open2(pCodecCtx, pCodec, NULL);
    if (ret < 0) {
        ALOGE("avcodec_open2 error!!");
        return avError(ret);
    }

    ALOGI("w = %d,h = %d", pCodecCtx->width, pCodecCtx->height);
    //解码，就对应了 解码器前的数据，压缩数据 AVPacket 解码后的数据 AVFrame 就是我们需要的YUV数据
    //先给AVFrame分配内存空间
    AVFrame *pFrameYUV = av_frame_alloc();
    //pCodecCtx->pix_fmt == AV_PIX_FMT_YUV420P??
    int buffer_size = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pCodecCtx->width,
                                               pCodecCtx->height, 1);
    uint8_t *buffers = (uint8_t *) av_malloc(buffer_size);
    //将buffers 的地址赋给 AVFrame
    av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, buffers, AV_PIX_FMT_YUV420P,
                         pCodecCtx->width, pCodecCtx->height, 1);


    //开始准备sdl的部分
    //SDL 四大要  window render texture  surface
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Event event;
    SDL_Rect sdlRect;
    SDL_Thread *video_tid;

    //初始化SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        ALOGE("Could not initialize SDL - %s", SDL_GetError());
        return 1;
    }

    //创建窗口  位置是中间。大小是0 ，SDL创建窗口的时候，大小都是0
    window = SDL_CreateWindow("SDL_Window", SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED, pCodecCtx->width, pCodecCtx->height,
                              SDL_WINDOW_RESIZABLE|SDL_WINDOW_FULLSCREEN | SDL_WINDOW_OPENGL);
    if (!window) {
        ALOGE("SDL:could not set video mode -exiting!!!\n");
        return -1;
    }
    //创建Renderer -1 表示使用默认的窗口 后面一个是Renderer的方式，0的话，应该就是未指定把？？？
    renderer = SDL_CreateRenderer(window, -1, 0);

    //这里的YU12 对应YUV420P ,SDL_TEXTUREACCESS_STREAMING 是表示texture 是不断被刷新的。
    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YV12,
                                             SDL_TEXTUREACCESS_STREAMING, pCodecCtx->width,
                                             pCodecCtx->height);


    // 设置显示的大小
    sdlRect.x = 0;
    sdlRect.y = 0;
    sdlRect.w = pCodecCtx->width;
    sdlRect.h = pCodecCtx->height;

    //准备好了Window 开始准备解码的数据
    AVPacket *packet = (AVPacket *) av_malloc(sizeof(AVPacket));

//    video_tid
    int yuv_width = pCodecCtx->width * pCodecCtx->height;
    av_new_packet(packet, yuv_width);

    //当你需要对齐进行缩放和转化的时候，需要先申请一个SwsContext
    SwsContext *img_convert = sws_getContext(pCodecCtx->width, pCodecCtx->height,
                                             pCodecCtx->pix_fmt,
                                             pCodecCtx->width,
                                             pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC,
                                             NULL, NULL, NULL);

//    video_tid = SDL_CreateThread(sfp_refresh_thread, NULL, NULL);
//
//    //进行解码
//    //读取
//    for (;;) {
//        SDL_WaitEvent(&event);
//        if (event.type == SFM_REFRESH_EVENT) {
//            while (1) {
//                if (av_read_frame(pFormatCtx, packet) < 0)
//                    thread_exit = 1;
//                if (packet->stream_index == video_stream)
//                    break;
//            }
//            int gop = avcodec_send_packet(pCodecCtx, packet);
//            //如果成功获取一帧的数据
//            if (gop == 0) {
//                //使用pFrame接受数据
//                ret = avcodec_receive_frame(pCodecCtx, pFrameYUV);
////                if (ret < 0) {
////                    ALOGE("decode error!!\n");
////                    return avError(ret);
////                }
//                if (ret == 0) {
//                    //进行缩放
//                    sws_scale(img_convert, pFrameYUV->data, pFrameYUV->linesize, 0,
//                              pCodecCtx->height,
//                              pFrameYUV->data, pFrameYUV->linesize);
//                    // iPitch 计算yuv一行数据占的字节数
//                    SDL_UpdateTexture(texture, NULL,
//                                      pFrameYUV->data[0], pFrameYUV->linesize[0]
//                    );
////                    SDL_UpdateYUVTexture(texture, &sdlRect,
////                                         pFrameYUV->data[0], pFrameYUV->linesize[0],
////                                         pFrameYUV->data[1], pFrameYUV->linesize[1],
////                                         pFrameYUV->data[2], pFrameYUV->linesize[2]);
//
//                    //清空数据
//                    SDL_RenderClear(renderer);
//                    //复制数据
//                    SDL_RenderCopy(renderer, texture, &sdlRect, &sdlRect);
//                    //渲染到屏幕
//                    SDL_RenderPresent(renderer);
//                    //延迟40
//                } else if (ret == AVERROR(EAGAIN)) {
//                    ALOGE("%s", "Frame is not available right, please try another input");
//                } else if (ret == AVERROR_EOF) {
//                    ALOGE("%s", "the decoder has been fully flushed");
//                } else if (ret == AVERROR(EINVAL)) {
//                    ALOGE("%s", "codec not opened, or it is an encoder");
//                } else {
//                    ALOGI("%s", "legitimate decoding errors");
//                }
//                av_packet_unref(packet);
//            } else if (event.type == SDL_KEYDOWN) {
//                if (event.key.keysym.sym == SDLK_AC_BACK) {
//                    thread_pause = !thread_pause;
//                }
//            } else if (event.type == SDL_QUIT) {
//                thread_exit = 1;
////                break;
//            } else if (event.type == SFM_BREAK_EVENT) {
//                break;
//            }
//        }
//    }

    while (av_read_frame(pFormatCtx, packet) >= 0) {
        if (packet->stream_index == video_stream) {
            //送入解码器
            int gop = avcodec_send_packet(pCodecCtx, packet);
            //如果成功获取一帧的数据
            if (gop == 0) {
                //使用pFrame接受数据
                ret = avcodec_receive_frame(pCodecCtx, pFrameYUV);
//                if (ret < 0) {
//                    ALOGE("decode error!!\n");
//                    return avError(ret);
//                }
                if (ret == 0) {
                //进行缩放
                    sws_scale(img_convert, pFrameYUV
                                      ->data, pFrameYUV->linesize, 0,
                              pCodecCtx->height,
                              pFrameYUV->data, pFrameYUV->linesize);
                // iPitch 计算yuv一行数据占的字节数
                    SDL_UpdateYUVTexture(texture, &sdlRect,
                                         pFrameYUV
                                                 ->data[0], pFrameYUV->linesize[0],
                                         pFrameYUV->data[1], pFrameYUV->linesize[1],
                                         pFrameYUV->data[2], pFrameYUV->linesize[2]);

                    //清空数据
                    SDL_RenderClear(renderer);
                    //复制数据
                    SDL_RenderCopy(renderer, texture, &sdlRect, &sdlRect
                    );
                    //渲染到屏幕
                    SDL_RenderPresent(renderer);
                    //延迟40 25 fps???
                    SDL_Delay(5);
                } else if (ret == AVERROR(EAGAIN)) {
                    ALOGE("%s", "Frame is not available right, please try another input");
                } else if (ret == AVERROR_EOF) {
                    ALOGE("%s", "the decoder has been fully flushed");
                } else if (ret == AVERROR(EINVAL)) {
                    ALOGE("%s", "codec not opened, or it is an encoder");
                } else {
                    ALOGI("%s", "legitimate decoding errors");
                }
            }
        }

        av_packet_unref(packet);

        if (
                SDL_PollEvent(&event)
                ) {
            SDL_bool needToQuit = SDL_FALSE;
            switch (event.type) {
                case SDL_QUIT:
                case SDL_KEYDOWN:
                    needToQuit = SDL_TRUE;
                    break;
                default:
                    break;
            }

            if (needToQuit) {
                break;
            }
        }
    }


//播放完成了。
//销毁texture
    SDL_DestroyTexture(texture);

    SDL_DestroyRenderer(renderer);

    SDL_DestroyWindow(window);

    SDL_Quit();


    sws_freeContext(img_convert);
    av_free(buffers);
    av_free(pFrameYUV);
    avcodec_parameters_free(&pFormatCtx
            ->streams[video_stream]->codecpar);
    avcodec_close(pCodecCtx);
    avformat_close_input(&pFormatCtx);

    return 0;
}
*/


//把显示图片的原来的main方法给注释掉了
extern "C"
//这里是直接定义了SDL的main方法吗
int main(int argc, char *argv[]) {

    // 打印ffmpeg信息
    const char *str = avcodec_configuration();
    ALOGI("avcodec_configuration: %s", str);

    char *video_path = argv[1];
    ALOGI("video_path  : %s", video_path);

    //开始准备sdl的部分
    //SDL 要素  window render texture
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Event event;

    //初始化SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        ALOGE("Could not initialize SDL - %s", SDL_GetError());
        return 1;
    }

    //创建窗口  位置是中间。大小是0 ，SDL创建窗口的时候，大小都是0
    window = SDL_CreateWindow("SDL_Window", SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED, 0, 0, SDL_WINDOW_SHOWN);
    //创建Renderer -1 表示使用默认的窗口 后面一个是Renderer的方式，0的话，应该就是未指定把？？？
    renderer = SDL_CreateRenderer(window, -1, 0);

    //因为只是简单展示一个图片，所以就创建一个Surface
    SDL_Surface *bmp = SDL_LoadBMP("image.bmp");

    //设置图中的透明色。
    SDL_SetColorKey(bmp, SDL_TRUE, 0xffffff);

    //创建一个texture
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, bmp);
    //清楚所有的事件？
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);

    //进入主循环，就是不断的刷新。这个应该是根据屏幕刷新率去刷新吗？
    while (1) {
        if (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                break;
            }
        }

        //先填充窗口的颜色
        SDL_SetRenderDrawColor(renderer, 0, 133, 119, 255);
        SDL_RenderClear(renderer);

        //RenderCopy RenderPresent 后面两个矩阵，可以分配这个texture的大小
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        //刷新屏幕
        SDL_RenderPresent(renderer);
    }

    SDL_FreeSurface(bmp);

    SDL_DestroyTexture(texture);

    SDL_DestroyRenderer(renderer);

    SDL_DestroyWindow(window);

    SDL_Quit();

    return 0;
}

