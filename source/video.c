/*
 * video.c - full screen video output built on SDL2.
 *
 * Frames arrive as BGRA (SDL_PIXELFORMAT_ARGB8888 in memory on little
 * endian), are uploaded to a streaming texture and drawn scaled to the
 * screen with black letterbox bars when the aspect ratio differs.
 */

#include "video.h"

#include "log.h"

#include <SDL2/SDL.h>
#include <math.h>
#include <switch.h>

static SDL_Window   *s_window;
static SDL_Renderer *s_renderer;
static SDL_Texture  *s_texture;
static int           s_tex_w;
static int           s_tex_h;

bool video_init(void)
{
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

    s_window = SDL_CreateWindow("Grand Theft Auto VI",
                                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                1920, 1080, SDL_WINDOW_FULLSCREEN);
    if (s_window == NULL) {
        log_line("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }

    s_renderer = SDL_CreateRenderer(s_window, -1, SDL_RENDERER_ACCELERATED);
    if (s_renderer == NULL) {
        log_line("accelerated renderer failed (%s), trying software", SDL_GetError());
        s_renderer = SDL_CreateRenderer(s_window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (s_renderer == NULL) {
        log_line("SDL_CreateRenderer failed: %s", SDL_GetError());
        return false;
    }

    {
        SDL_RendererInfo info;

        if (SDL_GetRendererInfo(s_renderer, &info) == 0)
            log_line("renderer: %s", info.name);
    }

    SDL_ShowCursor(SDL_DISABLE);
    SDL_SetRenderDrawColor(s_renderer, 0, 0, 0, 0xFF);
    SDL_RenderClear(s_renderer);
    SDL_RenderPresent(s_renderer);

    return true;
}

void video_exit(void)
{
    if (s_texture != NULL) {
        SDL_DestroyTexture(s_texture);
        s_texture = NULL;
    }
    if (s_renderer != NULL) {
        SDL_DestroyRenderer(s_renderer);
        s_renderer = NULL;
    }
    if (s_window != NULL) {
        SDL_DestroyWindow(s_window);
        s_window = NULL;
    }
    s_tex_w = s_tex_h = 0;
}

void video_show(const uint8_t *pixels, int stride, int width, int height)
{
    if (s_renderer == NULL || pixels == NULL || width <= 0 || height <= 0)
        return;

    if (s_texture == NULL || width != s_tex_w || height != s_tex_h) {
        if (s_texture != NULL)
            SDL_DestroyTexture(s_texture);
        s_texture = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_ARGB8888,
                                      SDL_TEXTUREACCESS_STREAMING, width, height);
        if (s_texture == NULL) {
            s_tex_w = s_tex_h = 0;
            return;
        }
        s_tex_w = width;
        s_tex_h = height;
    }

    if (SDL_UpdateTexture(s_texture, NULL, pixels, stride) != 0)
        return;

    int out_w = 1920;
    int out_h = 1080;
    SDL_GetRendererOutputSize(s_renderer, &out_w, &out_h);
    if (out_w <= 0 || out_h <= 0) {
        out_w = 1920;
        out_h = 1080;
    }

    double scale_x = (double)out_w / (double)width;
    double scale_y = (double)out_h / (double)height;
    double scale = scale_x < scale_y ? scale_x : scale_y;

    SDL_Rect dst;
    dst.w = (int)lround((double)width * scale);
    dst.h = (int)lround((double)height * scale);
    dst.x = (out_w - dst.w) / 2;
    dst.y = (out_h - dst.h) / 2;

    SDL_RenderClear(s_renderer);
    SDL_RenderCopy(s_renderer, s_texture, NULL, &dst);
}

void video_present(void)
{
    if (s_renderer != NULL)
        SDL_RenderPresent(s_renderer);
}
