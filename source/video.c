/*
 * video.c - full screen video output through the libnx framebuffer.
 *
 * The framebuffer is created on the default window in RGBA8, in linear mode
 * so the decoder can write rows at a known stride. framebufferEnd() hands the
 * picture to the compositor, which is all the "presenting" a video player
 * needs.
 */

#include "video.h"

#include "log.h"

#include <switch.h>

#include <string.h>

#define SCREEN_WIDTH  1920
#define SCREEN_HEIGHT 1080

static Framebuffer s_fb;
static bool        s_ready;

bool video_init(void)
{
    Result rc;

    rc = framebufferCreate(&s_fb, nwindowGetDefault(),
                           SCREEN_WIDTH, SCREEN_HEIGHT,
                           PIXEL_FORMAT_RGBA_8888, 2);
    if (R_FAILED(rc)) {
        log_line("framebufferCreate failed: 0x%x", rc);
        return false;
    }

    /* Linear layout: the shadow buffer is a plain width*4 byte row buffer,
     * which libswscale can fill directly. */
    rc = framebufferMakeLinear(&s_fb);
    if (R_FAILED(rc)) {
        log_line("framebufferMakeLinear failed: 0x%x", rc);
        framebufferClose(&s_fb);
        return false;
    }

    s_ready = true;

    /* Start from a black screen: nothing is displayed before the first frame. */
    {
        u32 stride = 0;
        uint8_t *pixels = framebufferBegin(&s_fb, &stride);

        if (pixels != NULL)
            memset(pixels, 0, (size_t)stride * SCREEN_HEIGHT);
        framebufferEnd(&s_fb);
    }

    log_line("framebuffer ready: %dx%d", SCREEN_WIDTH, SCREEN_HEIGHT);
    return true;
}

void video_exit(void)
{
    if (s_ready) {
        framebufferClose(&s_fb);
        s_ready = false;
    }
}

void video_output_size(int *width, int *height)
{
    if (width != NULL)
        *width = SCREEN_WIDTH;
    if (height != NULL)
        *height = SCREEN_HEIGHT;
}

uint8_t *video_begin_frame(uint32_t *stride)
{
    u32 row_stride = 0;
    uint8_t *pixels;

    if (!s_ready)
        return NULL;

    pixels = framebufferBegin(&s_fb, &row_stride);
    if (stride != NULL)
        *stride = row_stride;

    return pixels;
}

void video_end_frame(void)
{
    if (s_ready)
        framebufferEnd(&s_fb);
}
