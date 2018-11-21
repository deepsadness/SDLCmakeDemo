#include "SDL.h"
#include "log.h"


//把显示图片的原来的main方法给注释掉了
extern "C"
//这里是直接定义了SDL的main方法吗
int main00(int argc, char *argv[]) {

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