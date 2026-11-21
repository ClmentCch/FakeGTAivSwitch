/*
 * audio.h - sound output through libnx audout and the playback clock.
 *
 * The audio device is the master clock of the player: video frames are
 * presented when their presentation timestamp is reached on the audio
 * timeline, which keeps picture and sound locked together.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Opens the audio output (48000 Hz, signed 16-bit, stereo).
 * Returns false when no audio device is available; the player then falls back
 * to a wall clock and keeps showing the video. */
bool audio_init(void);
void audio_exit(void);

/* True when the audio device was opened successfully. */
bool audio_available(void);

/* Sample rate actually used by the device. */
int audio_rate(void);

/* Drops everything queued, silences the device and restarts the clock. */
void audio_start(void);

/* Queues interleaved stereo s16 samples. */
void audio_push(const int16_t *frames, int count);

/* Feeds free device buffers from the queue. Called regularly by the player. */
void audio_pump(void);

/* Frames waiting to be played (not yet heard). */
int audio_queued_frames(void);

/* When enabled the clock is derived from the system timer instead of the
 * audio device (used when a file has no usable audio track). */
void audio_use_wallclock(bool enabled);

/* Seconds of playback elapsed since audio_start(). */
double audio_clock(void);
