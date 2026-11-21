/*
 * media.h - MP4 demuxing, decoding and playback.
 */

#pragma once

#include <stdbool.h>

/* The application always reads the file from this location. */
#define VIDEO_PATH "sdmc:/switch/gtaVI/video.mp4"

typedef struct Media Media;

/* Called while the player waits (frame pacing, audio back pressure).
 * Returning false aborts playback (used for the "+" exit button). */
typedef bool (*MediaIdleFn)(void *user);

/* Opens and probes the media file. Returns NULL on failure. */
Media *media_open(const char *path);
void   media_close(Media *media);

/* Decodes the first video frame and displays it. */
bool media_first_frame(Media *media);

/* Rewinds, then plays the file once, from the first frame to the end. */
bool media_play(Media *media, MediaIdleFn idle, void *user);
