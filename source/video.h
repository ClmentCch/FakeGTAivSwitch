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
bool video_init(void);
void video_exit(void);

/* Size of the display surface. */
void video_output_size(int *width, int *height);

/* Returns the memory of the frame being written and its row stride in bytes.
 * The caller fills it with RGBA pixels and then calls video_end_frame(). */
uint8_t *video_begin_frame(uint32_t *stride);
void video_end_frame(void);
