/*
 * media.h - MP4 demuxing, decoding and playback.
 */

#pragma once

#include <stdbool.h>

/* The application always reads the file from this location. */
#define VIDEO_PATH "sdmc:/switch/gtaVI/video.mp4"

typedef struct Player Player;

/* Called while the player waits (frame pacing, audio back pressure).
 * Returning false aborts playback (used for the "+" exit button). */
typedef bool (*PlayerIdle)(void *user);

/* Opens and probes the media file. Returns NULL on failure. */
Player *player_open(const char *path);
void   player_close(Player *media);

/* Decodes the first video frame and displays it. */
bool player_first_frame(Player *media);

/* Rewinds, then plays the file once, from the first frame to the end. */
bool player_play(Player *media, PlayerIdle idle, void *user);
