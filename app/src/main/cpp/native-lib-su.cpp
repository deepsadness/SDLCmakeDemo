#include "SDL.h"
#include "log.h"

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/avutil.h"
#include "libavutil/imgutils.h"
#include "libswscale/swscale.h"
}

//参考，加上了SDL event的版本

//自己定义两个事件
//刷新的Event
#define SFM_REFRESH_EVENT (SDL_USEREVENT+1)
//退出的Event
#define SFM_BREAK_EVENT (SDL_USEREVENT+2)

int thread_exit = 0;
int thread_pause = 0;

//创建了一个线程，不断给自己发送刷新的事件
int sfp_refresh_thread(void *opaque) {
    thread_exit = 0;
    thread_pause = 0;

    while (!thread_exit) {
        if (!thread_pause) {
            SDL_Event event;
            event.type = SFM_REFRESH_EVENT;
            SDL_PushEvent(&event);
        }
        //为什么同样不能使用延迟呢？
//        SDL_Delay(40);
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
                              SDL_WINDOW_RESIZABLE | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_OPENGL);
    if (!window) {
        ALOGE("SDL:could not set video mode -exiting!!!\n");
        return -1;
    }
    //创建Renderer -1 表示使用默认的窗口 后面一个是Renderer的方式，0的话，应该就是未指定把？？？
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC);

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

    //创造线程，开始等待
    video_tid = SDL_CreateThread(sfp_refresh_thread, NULL, NULL);

    //创建一个死循环
    for (;;) {
        //开始等待回调
        SDL_WaitEvent(&event);
        if (event.type == SFM_REFRESH_EVENT) {  //如果是我们自己刷新事件的话，就开始刷新
            if (av_read_frame(pFormatCtx, packet) >= 0) {
                if (packet->stream_index == video_stream) {
                    int gop = avcodec_send_packet(pCodecCtx, packet);
                    if (gop == 0) {
                        ret = avcodec_receive_frame(pCodecCtx, pFrameYUV);
                        if (ret == 0) {
                            //进行缩放。这里可以用libyuv进行转换
                            sws_scale(img_convert,
                                      reinterpret_cast<const uint8_t *const *>(pFrameYUV
                                              ->data), pFrameYUV->linesize, 0,
                                      pCodecCtx->height,
                                      pFrameYUV->data, pFrameYUV->linesize);
                            //应为是YUV，所以调用UpdateYUV方法，分别将YUV填充进去
                            SDL_UpdateYUVTexture(texture, &sdlRect,
                                                 pFrameYUV
                                                         ->data[0], pFrameYUV->linesize[0],
                                                 pFrameYUV->data[1], pFrameYUV->linesize[1],
                                                 pFrameYUV->data[2], pFrameYUV->linesize[2]);

                            //清空数据
                            SDL_RenderClear(renderer);
                            //复制数据
                            SDL_RenderCopy(renderer, texture, &sdlRect, &sdlRect);
                            //渲染到屏幕
                            SDL_RenderPresent(renderer);
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
                    av_packet_unref(packet);
                }
            }
        } else if (event.type == SDL_QUIT) {
            thread_exit = 1;
        } else if (event.type == SFM_BREAK_EVENT) {
            break;
        } else {
            //其他事件，就简单的当做是暂停
            thread_pause = !thread_pause;
        }
    }

    //SDL资源释放
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    //FFmpeg资源释放
    sws_freeContext(img_convert);
    av_free(buffers);
    av_free(pFrameYUV);
    avcodec_parameters_free(&pFormatCtx
            ->streams[video_stream]->codecpar);
    avcodec_close(pCodecCtx);
    avformat_close_input(&pFormatCtx);

    return 0;
}