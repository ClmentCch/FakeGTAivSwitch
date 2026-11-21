/*
 * audio.h - SDL audio output and playback clock.
 *
 * The audio device is the master clock of the player: video frames are
 * presented when their presentation timestamp is reached on the audio
 * timeline, which keeps picture and sound locked together.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Opens the default audio device (48 kHz, signed 16-bit, stereo).
 * Returns false when no audio device is available; the player then falls
 * back to a wall clock. */
bool audio_init(void);
void audio_exit(void);

/* Sample rate actually used by the device. */
int audio_rate(void);

/* Clears the queue, pauses the device and restarts the clock. */
void audio_start(void);

/* Queues interleaved stereo s16 samples. */
void audio_push(const int16_t *frames, int count);

/* Number of frames currently waiting in the device queue. */
int audio_queued_frames(void);

/* When enabled the clock is derived from the system timer instead of the
 * audio device (used when a file has no usable audio track). */
void audio_use_wallclock(bool enabled);

/* Seconds of playback elapsed since audio_start(). */
double audio_clock(void);
