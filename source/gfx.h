/*
 * video.h - full screen video output through the libnx framebuffer.
 *
 * No SDL, no OpenGL: the decoder converts its picture straight into the
 * console framebuffer, which is the simplest and most reliable path for a
 * homebrew application (it is the same path the console text renderer uses).
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Creates the full screen (1920x1080) framebuffer. */
bool gfx_init(void);
void gfx_exit(void);

/* Size of the display surface. */
void gfx_size(int *width, int *height);

/* Returns the memory of the frame being written and its row stride in bytes.
 * The caller fills it with RGBA pixels and then calls gfx_end_frame(). */
uint8_t *gfx_begin_frame(uint32_t *stride);
void gfx_end_frame(void);
