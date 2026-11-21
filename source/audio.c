/*
 * audio.c - sound output through libnx audout and the playback clock.
 *
 * audout plays fixed buffers that are handed to it: the decoder's samples are
 * accumulated in a ring buffer, then copied into device buffers as they are
 * released. The device consumes samples at the hardware rate, so the number of
 * frames handed out minus the frames still queued is exactly the playback
 * position, which is used as the master clock.
 *
 * Feeding the device happens in a dedicated thread: video decoding can take a
 * long time on a heavy file, and the audio device only holds a fraction of a
 * second of samples. Without its own thread the device underruns and the sound
 * crackles as soon as one picture takes too long to decode.
 */

#include "audio.h"

#include "log.h"
#include "timebase.h"

#include <switch.h>

#include <stdlib.h>
#include <string.h>

#define AUDIO_RATE     48000
#define AUDIO_CHANNELS 2
#define BYTES_PER_FRAME (AUDIO_CHANNELS * sizeof(int16_t))

/* 3 seconds of slack between decoding and the device. */
#define RING_BYTES (AUDIO_RATE * 3 * BYTES_PER_FRAME)

/* 1024 frames = 4096 bytes = 0x1000, the alignment audout requires. Six
 * buffers give about 128 ms of headroom in the device itself. */
#define BUFFER_FRAMES 1024
#define BUFFER_BYTES  (BUFFER_FRAMES * BYTES_PER_FRAME)
#define BUFFER_COUNT  6

/* audout wants buffers aligned to 0x1000 bytes. */
static void *alloc_aligned(size_t size)
{
    size_t rounded = (size + 0xFFFu) & ~(size_t)0xFFFu;

    return aligned_alloc(0x1000, rounded);
}

static bool s_ready;
static u32  s_rate = AUDIO_RATE;

static Mutex s_lock;

static uint8_t *s_ring;
static u32      s_head;
static u32      s_used;

static AudioOutBuffer s_buffers[BUFFER_COUNT];
static void          *s_buffer_mem[BUFFER_COUNT];
static u32            s_buffer_frames[BUFFER_COUNT];
static int            s_free[BUFFER_COUNT];
static int            s_free_count;

static uint64_t s_written;       /* frames handed to the device         */
static uint64_t s_queued;        /* frames still inside the device      */
static double   s_consumed;      /* last observed played frame count    */
static double   s_stall_offset;  /* frames added when the device stalls */
static uint64_t s_progress_ms;
static uint64_t s_start_ms;
static bool     s_wallclock;

static Thread         s_thread;
static volatile bool  s_thread_stop;
static bool           s_thread_started;

/* ------------------------------------------------------------------------- */
/* Ring buffer (all accesses happen with s_lock held)                         */
/* ------------------------------------------------------------------------- */

static void ring_write(const uint8_t *src, u32 bytes)
{
    u32 first = RING_BYTES - s_head;

    if (bytes > first) {
        memcpy(s_ring + s_head, src, first);
        memcpy(s_ring, src + first, bytes - first);
        s_head = bytes - first;
    } else {
        memcpy(s_ring + s_head, src, bytes);
        s_head += bytes;
        if (s_head == RING_BYTES)
            s_head = 0;
    }

    s_used += bytes;
}

static void ring_read(uint8_t *dst, u32 bytes)
{
    u32 tail = (s_head + RING_BYTES - s_used) % RING_BYTES;
    u32 first = RING_BYTES - tail;

    if (bytes > first) {
        memcpy(dst, s_ring + tail, first);
        memcpy(dst + first, s_ring, bytes - first);
    } else {
        memcpy(dst, s_ring + tail, bytes);
    }

    s_used -= bytes;
}

/* ------------------------------------------------------------------------- */
/* Device buffers                                                            */
/* ------------------------------------------------------------------------- */

static int buffer_index(void *mem)
{
    for (int i = 0; i < BUFFER_COUNT; i++) {
        if (s_buffer_mem[i] == mem)
            return i;
    }
    return -1;
}

static void buffer_release(int index)
{
    s_free[s_free_count++] = index;
}

static int buffer_take(void)
{
    if (s_free_count == 0)
        return -1;
    return s_free[--s_free_count];
}

/* Collects the buffers the device has finished playing. */
static void collect_released(void)
{
    for (;;) {
        AudioOutBuffer *released = NULL;
        u32 count = 0;
        int index;

        if (R_FAILED(audoutGetReleasedAudioOutBuffer(&released, &count)) || count == 0)
            return;

        for (u32 i = 0; i < count; i++) {
            u32 frames;

            index = buffer_index(released[i].buffer);
            if (index < 0)
                continue;

            /* The frame count is remembered when the buffer is submitted: the
             * service may report the whole buffer instead of the data actually
             * queued, and a wrong count stalls the clock and the frame pacing. */
            frames = s_buffer_frames[index];
            s_buffer_frames[index] = 0;

            if (s_queued >= frames)
                s_queued -= frames;
            else
                s_queued = 0;

            buffer_release(index);
        }
    }
}

static void pump_locked(void)
{
    collect_released();

    while (s_used > 0) {
        int index = buffer_take();
        u32 frames;
        u32 bytes;

        if (index < 0)
            break;

        frames = s_used / BYTES_PER_FRAME;
        if (frames > BUFFER_FRAMES)
            frames = BUFFER_FRAMES;
        if (frames == 0) {
            buffer_release(index);
            break;
        }

        bytes = frames * BYTES_PER_FRAME;
        ring_read(s_buffer_mem[index], bytes);

        s_buffers[index].next = NULL;
        s_buffers[index].buffer = s_buffer_mem[index];
        s_buffers[index].buffer_size = BUFFER_BYTES;
        s_buffers[index].data_size = bytes;
        s_buffers[index].data_offset = 0;

        if (R_FAILED(audoutAppendAudioOutBuffer(&s_buffers[index]))) {
            buffer_release(index);
            break;
        }

        s_buffer_frames[index] = frames;
        s_written += frames;
        s_queued += frames;
    }
}

void audio_pump(void)
{
    if (!s_ready)
        return;

    mutexLock(&s_lock);
    pump_locked();
    mutexUnlock(&s_lock);
}

/* Keeps the device fed whatever the video decoder is doing. */
static void audio_thread(void *arg)
{
    (void)arg;

    while (!s_thread_stop) {
        audio_pump();
        svcSleepThread(4 * 1000 * 1000ULL);
    }
}

/* ------------------------------------------------------------------------- */
/* Setup                                                                     */
/* ------------------------------------------------------------------------- */

bool audio_init(void)
{
    /* audout expects real buffers for the device names, so the first device
     * reported by the service is passed in by name. */
    char names[0x100 * 4];
    char device_out[0x100];
    const char *device_in = NULL;
    s32 name_capacity = 4;
    u32 name_count = 0;
    PcmFormat format = PcmFormat_Int16;
    AudioOutState state = AudioOutState_Stopped;
    u32 channels_out = 0;
    Result rc;

    mutexInit(&s_lock);

    rc = audoutInitialize();
    if (R_FAILED(rc)) {
        log_line("audoutInitialize failed: 0x%x", rc);
        return false;
    }

    if (R_SUCCEEDED(audoutListAudioOuts(names, name_capacity, &name_count)) && name_count > 0)
        device_in = names;

    rc = audoutOpenAudioOut(device_in, device_out, AUDIO_RATE, AUDIO_CHANNELS,
                            &s_rate, &channels_out, &format, &state);
    if (R_FAILED(rc) || format != PcmFormat_Int16 || channels_out != AUDIO_CHANNELS) {
        log_line("audoutOpenAudioOut failed: 0x%x (format %d, channels %u)",
                 rc, (int)format, channels_out);
        audoutExit();
        return false;
    }

    s_ring = alloc_aligned(RING_BYTES);
    if (s_ring == NULL) {
        audoutExit();
        return false;
    }

    for (int i = 0; i < BUFFER_COUNT; i++) {
        s_buffer_mem[i] = alloc_aligned(BUFFER_BYTES);
        if (s_buffer_mem[i] == NULL) {
            audoutExit();
            return false;
        }
        s_free[s_free_count++] = i;
    }

    rc = audoutStartAudioOut();
    if (R_FAILED(rc)) {
        log_line("audoutStartAudioOut failed: 0x%x", rc);
        audoutExit();
        return false;
    }

    s_ready = true;
    audio_start();

    s_thread_stop = false;
    rc = threadCreate(&s_thread, audio_thread, NULL, NULL, 0x4000, 0x20, -2);
    if (R_SUCCEEDED(rc))
        rc = threadStart(&s_thread);
    if (R_SUCCEEDED(rc)) {
        s_thread_started = true;
    } else {
        log_line("audio thread failed: 0x%x", rc);
        s_thread_started = false;
    }

    return true;
}

void audio_exit(void)
{
    if (!s_ready)
        return;

    if (s_thread_started) {
        s_thread_stop = true;
        threadWaitForExit(&s_thread);
        threadClose(&s_thread);
        s_thread_started = false;
    }

    audoutStopAudioOut();
    audoutExit();

    free(s_ring);
    s_ring = NULL;

    for (int i = 0; i < BUFFER_COUNT; i++) {
        free(s_buffer_mem[i]);
        s_buffer_mem[i] = NULL;
    }

    s_free_count = 0;
    s_ready = false;
}

bool audio_available(void)
{
    return s_ready;
}

int audio_rate(void)
{
    return (int)s_rate;
}

void audio_start(void)
{
    if (!s_ready) {
        s_start_ms = time_ms();
        s_wallclock = true;
        return;
    }

    mutexLock(&s_lock);

    s_head = 0;
    s_used = 0;
    s_written = 0;
    s_queued = 0;
    s_consumed = 0.0;
    s_stall_offset = 0.0;
    s_progress_ms = time_ms();
    s_start_ms = s_progress_ms;
    s_wallclock = false;

    /* Whatever was queued belongs to the previous loop: drop it so nothing is
     * heard during the two seconds the first picture is held. */
    {
        bool flushed = false;

        audoutFlushAudioOutBuffers(&flushed);

        for (int i = 0; i < 8; i++) {
            AudioOutBuffer *released = NULL;
            u32 count = 0;

            if (R_FAILED(audoutGetReleasedAudioOutBuffer(&released, &count)) || count == 0)
                break;

            for (u32 k = 0; k < count; k++) {
                int index = buffer_index(released[k].buffer);
                if (index >= 0 && s_free_count < BUFFER_COUNT) {
                    s_buffer_frames[index] = 0;
                    buffer_release(index);
                }
            }
        }
    }

    mutexUnlock(&s_lock);
}

void audio_push(const int16_t *frames, int count)
{
    u32 bytes;
    u32 space;

    if (!s_ready || frames == NULL || count <= 0)
        return;

    mutexLock(&s_lock);

    bytes = (u32)count * BYTES_PER_FRAME;
    space = RING_BYTES - s_used;

    if (bytes > space)
        bytes = space - (space % BYTES_PER_FRAME); /* never split a frame */

    if (bytes > 0)
        ring_write((const uint8_t *)frames, bytes);

    pump_locked();

    mutexUnlock(&s_lock);
}

int audio_queued_frames(void)
{
    int queued;

    if (!s_ready)
        return 0;

    mutexLock(&s_lock);
    collect_released();
    queued = (int)(s_used / BYTES_PER_FRAME + s_queued);
    mutexUnlock(&s_lock);

    return queued;
}

void audio_use_wallclock(bool enabled)
{
    s_wallclock = enabled || !s_ready;
    s_start_ms = time_ms();
}

double audio_clock(void)
{
    uint64_t now;
    double played;
    double result;

    if (s_wallclock || !s_ready)
        return (double)(time_ms() - s_start_ms) / 1000.0;

    mutexLock(&s_lock);

    collect_released();

    played = (double)s_written - (double)s_queued;
    now = time_ms();

    if (played < 0.0)
        played = 0.0;

    if (played > s_consumed) {
        s_consumed = played;
        s_progress_ms = now;
    } else if (now > s_progress_ms + 250) {
        /* The device stopped consuming samples: keep the timeline moving so
         * the picture never freezes. */
        s_stall_offset += (double)(now - s_progress_ms);
        s_progress_ms = now;
    }

    result = (played + s_stall_offset) / (double)audio_rate();

    mutexUnlock(&s_lock);

    return result;
}
