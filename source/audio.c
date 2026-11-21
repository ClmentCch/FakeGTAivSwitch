/*
 * audio.c - SDL audio output and playback clock.
 */

#include "audio.h"

#include <SDL2/SDL.h>
#include <switch.h>

static SDL_AudioDeviceID s_device;
static SDL_AudioSpec     s_spec;

static uint64_t s_written;      /* frames handed to the device            */
static double   s_consumed;     /* last observed number of played frames   */
static double   s_stall_offset; /* frames added when the device underruns  */
static uint64_t s_progress_ms;  /* last time the play position advanced    */
static uint64_t s_start_ms;     /* start of the wall-clock timeline        */
static bool     s_paused;
static bool     s_wallclock;

static int frame_size(void)
{
    return s_spec.channels * (int)sizeof(int16_t);
}

bool audio_init(void)
{
    SDL_AudioSpec want;

    SDL_zero(want);
    want.freq = 48000;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;

    /* The Switch audio engine may impose its own rate and buffer size, so
     * those two are allowed to change; the resampler follows the rate that is
     * finally obtained. */
    s_device = SDL_OpenAudioDevice(NULL, 0, &want, &s_spec,
                                   SDL_AUDIO_ALLOW_FREQUENCY_CHANGE |
                                   SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
    if (s_device == 0)
        return false;

    if (s_spec.format != AUDIO_S16SYS || s_spec.channels != 2) {
        SDL_CloseAudioDevice(s_device);
        s_device = 0;
        SDL_zero(s_spec);
        return false;
    }

    audio_start();
    return true;
}

void audio_exit(void)
{
    if (s_device != 0) {
        SDL_CloseAudioDevice(s_device);
        s_device = 0;
        SDL_zero(s_spec);
    }
}

bool audio_available(void)
{
    return s_device != 0;
}

int audio_rate(void)
{
    return s_spec.freq > 0 ? s_spec.freq : 48000;
}

void audio_start(void)
{
    s_written = 0;
    s_consumed = 0.0;
    s_stall_offset = 0.0;
    s_progress_ms = SDL_GetTicks64();
    s_start_ms = s_progress_ms;
    s_paused = true;
    s_wallclock = s_device == 0;

    if (s_device == 0)
        return;

    SDL_PauseAudioDevice(s_device, 1);
    SDL_ClearQueuedAudio(s_device);
}

void audio_push(const int16_t *frames, int count)
{
    if (s_device == 0 || frames == NULL || count <= 0)
        return;

    if (s_paused) {
        /* Start playing with the very first samples so no silence or
         * underrun is heard at the beginning of a loop. The timeline only
         * starts here, so nothing decoded before playback is counted. */
        SDL_PauseAudioDevice(s_device, 0);
        s_paused = false;
        s_progress_ms = SDL_GetTicks64();
        s_start_ms = s_progress_ms;
        s_consumed = 0.0;
        s_stall_offset = 0.0;
    }

    SDL_QueueAudio(s_device, frames, (Uint32)count * (Uint32)frame_size());
    s_written += (uint64_t)count;
}

int audio_queued_frames(void)
{
    if (s_device == 0)
        return 0;
    return (int)(SDL_GetQueuedAudioSize(s_device) / (Uint32)frame_size());
}

void audio_use_wallclock(bool enabled)
{
    s_wallclock = enabled || s_device == 0;
    s_start_ms = SDL_GetTicks64();
}

double audio_clock(void)
{
    uint64_t now;

    if (s_wallclock || s_device == 0)
        return (double)(SDL_GetTicks64() - s_start_ms) / 1000.0;

    double queued = (double)SDL_GetQueuedAudioSize(s_device) / (double)frame_size();
    double played = (double)s_written - queued;

    now = SDL_GetTicks64();

    if (played < 0.0)
        played = 0.0;

    if (played > s_consumed) {
        s_consumed = played;
        s_progress_ms = now;
    } else if (now > s_progress_ms + 250) {
        /* The device stopped consuming samples (underrun or no audio at
         * all). Keep the timeline moving so the picture never freezes. */
        s_stall_offset += (double)(now - s_progress_ms);
        s_progress_ms = now;
    }

    return (played + s_stall_offset) / (double)audio_rate();
}
