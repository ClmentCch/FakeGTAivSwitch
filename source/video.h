/*
 * video.h - full screen video output built on SDL2.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Creates the full screen window and the hardware renderer. */
bool video_init(void);
void video_exit(void);

/* Uploads one BGRA frame (the pixel format produced by libswscale) and
 * queues it for rendering, scaled to fit the screen while keeping the
 * source aspect ratio. */
void video_show(const uint8_t *pixels, int stride, int width, int height);

/* Presents the frame queued by video_show(). */
void video_present(void);
