/*
 * media.c - MP4 demuxing, decoding and playback.
 *
 * The file is opened with libnx's stdio (newlib devoptab) and handed to
 * FFmpeg through a custom AVIOContext. This is required because FFmpeg parses
 * "sdmc:/..." as a protocol named "sdmc", which does not exist in the file
 * protocol set.
 *
 * libavformat reads the container, libavcodec decodes the video and audio
 * streams, libswscale converts the decoded pictures to BGRA for the SDL
 * renderer and libswresample converts the audio to the stereo s16 stream the
 * audio device expects.
 *
 * The player runs on a single thread: the demuxer is read packet by packet,
 * audio is pushed to the audio queue (which is also the master clock) and
 * video frames are presented when their timestamp is reached on that clock.
 * Reading keeps going while the picture waits, so the audio queue never runs
 * dry for the duration of one frame.
 */

#include "media.h"

#include "audio.h"
#include "video.h"

#include <switch.h>

#include <SDL2/SDL_timer.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* At most half a second of audio is queued ahead of the device. */
#define AUDIO_QUEUE_HIGH_FRAMES 24000

/* Frames arriving later than this are dropped to keep sound and picture
 * together. */
#define VIDEO_LATE_TOLERANCE 0.045

/* Small head start that compensates the compositor/presentation latency. */
#define PRESENT_LEAD 0.008

/* Size of the buffer FFmpeg reads from the SD card through. */
#define AVIO_BUFFER_SIZE (64 * 1024)

struct Media {
    FILE            *file;
    AVIOContext     *avio;
    AVFormatContext *fmt;
    AVCodecContext  *vctx;
    AVCodecContext  *actx;
    struct SwsContext *sws;
    SwrContext      *swr;
    AVFrame         *frame;
    AVFrame         *rgb;
    AVPacket        *pkt;

    int     vs;
    int     as;
    double  vtb;
    int64_t vstart;
    int     vw;
    int     vh;

    int16_t *pcm;
    int      pcm_frames;

    double   last_pts;
};

/* ------------------------------------------------------------------------- */
/* SD card I/O for FFmpeg                                                    */
/* ------------------------------------------------------------------------- */

static int io_read(void *opaque, uint8_t *buf, int size)
{
    FILE *fp = opaque;
    size_t got = fread(buf, 1, (size_t)size, fp);

    if (got == 0)
        return ferror(fp) ? AVERROR(EIO) : AVERROR_EOF;

    return (int)got;
}

static int64_t io_seek(void *opaque, int64_t offset, int whence)
{
    FILE *fp = opaque;
    long current;

    if (whence == AVSEEK_SIZE) {
        current = ftell(fp);
        if (current < 0)
            return AVERROR(EIO);
        if (fseek(fp, 0, SEEK_END) != 0)
            return AVERROR(EIO);
        offset = (int64_t)ftell(fp);
        if (fseek(fp, current, SEEK_SET) != 0)
            return AVERROR(EIO);
        return offset;
    }

    whence &= ~AVSEEK_FORCE;

    if (fseek(fp, (long)offset, whence) != 0)
        return AVERROR(EIO);

    return (int64_t)ftell(fp);
}

/* ------------------------------------------------------------------------- */
/* Opening / closing                                                         */
/* ------------------------------------------------------------------------- */

static AVCodecContext *open_decoder(AVFormatContext *fmt, int index)
{
    AVStream *stream = fmt->streams[index];
    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    AVCodecContext *ctx;
    int ret;

    if (codec == NULL)
        return NULL;

    ctx = avcodec_alloc_context3(codec);
    if (ctx == NULL)
        return NULL;

    ret = avcodec_parameters_to_context(ctx, stream->codecpar);
    if (ret < 0) {
        avcodec_free_context(&ctx);
        return NULL;
    }

    ctx->pkt_timebase = stream->time_base;

    ret = avcodec_open2(ctx, codec, NULL);
    if (ret < 0) {
        avcodec_free_context(&ctx);
        return NULL;
    }

    return ctx;
}

Media *media_open(const char *path)
{
    Media *m = calloc(1, sizeof(*m));
    AVStream *vstream;
    uint8_t *iobuf;

    if (m == NULL)
        return NULL;

    m->vs = -1;
    m->as = -1;

    m->file = fopen(path, "rb");
    if (m->file == NULL)
        goto fail;

    iobuf = av_malloc(AVIO_BUFFER_SIZE);
    if (iobuf == NULL)
        goto fail;

    m->avio = avio_alloc_context(iobuf, AVIO_BUFFER_SIZE, 0,
                                 m->file, io_read, NULL, io_seek);
    if (m->avio == NULL) {
        av_free(iobuf);
        goto fail;
    }

    m->fmt = avformat_alloc_context();
    if (m->fmt == NULL)
        goto fail;

    m->fmt->pb = m->avio;
    m->fmt->flags |= AVFMT_FLAG_CUSTOM_IO;

    if (avformat_open_input(&m->fmt, NULL, NULL, NULL) < 0)
        goto fail;

    if (avformat_find_stream_info(m->fmt, NULL) < 0)
        goto fail;

    m->vs = av_find_best_stream(m->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (m->vs < 0)
        goto fail;

    m->as = av_find_best_stream(m->fmt, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);

    m->vctx = open_decoder(m->fmt, m->vs);
    if (m->vctx == NULL)
        goto fail;

    if (m->as >= 0) {
        m->actx = open_decoder(m->fmt, m->as);
        if (m->actx == NULL)
            m->as = -1; /* play silently instead of failing */
    }

    vstream = m->fmt->streams[m->vs];
    m->vtb = av_q2d(vstream->time_base);
    m->vstart = vstream->start_time == AV_NOPTS_VALUE ? 0 : vstream->start_time;

    m->pkt = av_packet_alloc();
    m->frame = av_frame_alloc();
    m->rgb = av_frame_alloc();
    if (m->pkt == NULL || m->frame == NULL || m->rgb == NULL)
        goto fail;

    return m;

fail:
    media_close(m);
    return NULL;
}

void media_close(Media *m)
{
    if (m == NULL)
        return;

    if (m->sws != NULL)
        sws_freeContext(m->sws);
    if (m->swr != NULL)
        swr_free(&m->swr);
    if (m->rgb != NULL)
        av_frame_free(&m->rgb);
    if (m->frame != NULL)
        av_frame_free(&m->frame);
    if (m->pkt != NULL)
        av_packet_free(&m->pkt);
    if (m->actx != NULL)
        avcodec_free_context(&m->actx);
    if (m->vctx != NULL)
        avcodec_free_context(&m->vctx);
    if (m->fmt != NULL)
        avformat_close_input(&m->fmt);

    if (m->avio != NULL) {
        av_freep(&m->avio->buffer);
        avio_context_free(&m->avio);
    }
    if (m->file != NULL)
        fclose(m->file);

    free(m->pcm);
    free(m);
}

/* ------------------------------------------------------------------------- */
/* Presentation                                                              */
/* ------------------------------------------------------------------------- */

static double frame_pts(Media *m, AVFrame *frame)
{
    double pts;

    if (frame->pts != AV_NOPTS_VALUE)
        pts = (double)(frame->pts - m->vstart) * m->vtb;
    else
        pts = m->last_pts + 1.0 / 30.0;

    if (pts < 0.0)
        pts = 0.0;

    m->last_pts = pts;
    return pts;
}

static bool convert_and_show(Media *m, AVFrame *frame)
{
    int ret;

    if (m->sws == NULL || m->vw != frame->width || m->vh != frame->height) {
        if (m->sws != NULL)
            sws_freeContext(m->sws);

        m->sws = sws_getContext(frame->width, frame->height, (enum AVPixelFormat)frame->format,
                                frame->width, frame->height, AV_PIX_FMT_BGRA,
                                SWS_BILINEAR, NULL, NULL, NULL);
        if (m->sws == NULL)
            return false;

        av_frame_unref(m->rgb);
        m->rgb->format = AV_PIX_FMT_BGRA;
        m->rgb->width = frame->width;
        m->rgb->height = frame->height;

        ret = av_frame_get_buffer(m->rgb, 32);
        if (ret < 0)
            return false;

        m->vw = frame->width;
        m->vh = frame->height;
    }

    ret = av_frame_make_writable(m->rgb);
    if (ret < 0)
        return false;

    sws_scale(m->sws,
              (const uint8_t * const *)frame->data, frame->linesize, 0, frame->height,
              m->rgb->data, m->rgb->linesize);

    video_show(m->rgb->data[0], m->rgb->linesize[0], m->vw, m->vh);
    return true;
}

static bool show_frame(Media *m, AVFrame *frame, MediaIdleFn idle, void *user, bool timed)
{
    double pts = frame_pts(m, frame);

    if (timed) {
        double wait_start = (double)SDL_GetTicks64() / 1000.0;

        if (audio_clock() - pts > VIDEO_LATE_TOLERANCE)
            return true; /* too late: drop it and stay in sync */

        for (;;) {
            double now = audio_clock();
            double wait;

            if (now + PRESENT_LEAD >= pts)
                break;

            /* Never block on a clock that stopped moving (for example when
             * the first audio samples have not been decoded yet). */
            if ((double)SDL_GetTicks64() / 1000.0 - wait_start > 0.150)
                break;

            if (!idle(user))
                return false;

            wait = pts - PRESENT_LEAD - now;
            if (wait > 0.010)
                wait = 0.010;
            if (wait < 0.001)
                wait = 0.001;
            svcSleepThread((u64)(wait * 1000000000.0));
        }
    }

    if (!convert_and_show(m, frame))
        return false;

    video_present();
    return true;
}

/* ------------------------------------------------------------------------- */
/* Decoding                                                                  */
/* ------------------------------------------------------------------------- */

static bool drain_video(Media *m, MediaIdleFn idle, void *user, bool timed)
{
    for (;;) {
        int ret = avcodec_receive_frame(m->vctx, m->frame);
        bool ok;

        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return true;
        if (ret < 0)
            return false;

        ok = show_frame(m, m->frame, idle, user, timed);
        av_frame_unref(m->frame);

        if (!ok)
            return false;
    }
}

static bool setup_resampler(Media *m)
{
    AVChannelLayout out_layout;
    int ret;

    if (m->swr != NULL)
        return true;

    av_channel_layout_default(&out_layout, 2);

    ret = swr_alloc_set_opts2(&m->swr,
                              &out_layout, AV_SAMPLE_FMT_S16, audio_rate(),
                              &m->actx->ch_layout, m->actx->sample_fmt, m->actx->sample_rate,
                              0, NULL);
    av_channel_layout_uninit(&out_layout);

    if (ret < 0)
        return false;

    return swr_init(m->swr) >= 0;
}

static bool drain_audio(Media *m, MediaIdleFn idle, void *user)
{
    for (;;) {
        int ret = avcodec_receive_frame(m->actx, m->frame);
        int out_max;
        int got;
        uint8_t *out[1];
        int16_t *buffer;

        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return true;
        if (ret < 0)
            return false;

        if (!setup_resampler(m)) {
            av_frame_unref(m->frame);
            return false;
        }

        out_max = (int)av_rescale_rnd(swr_get_delay(m->swr, m->actx->sample_rate) + m->frame->nb_samples,
                                      audio_rate(), m->actx->sample_rate, AV_ROUND_UP) + 64;

        if (out_max > m->pcm_frames) {
            buffer = realloc(m->pcm, (size_t)out_max * 2u * sizeof(int16_t));
            if (buffer == NULL) {
                av_frame_unref(m->frame);
                return false;
            }
            m->pcm = buffer;
            m->pcm_frames = out_max;
        }

        out[0] = (uint8_t *)m->pcm;
        got = swr_convert(m->swr, out, out_max,
                          (const uint8_t **)m->frame->extended_data, m->frame->nb_samples);
        av_frame_unref(m->frame);

        if (got < 0)
            return false;

        if (got > 0)
            audio_push(m->pcm, got);

        /* Back pressure: never let the queue grow without bound. The wait is
         * bounded so a dead audio device can never block the player. */
        if (audio_queued_frames() > AUDIO_QUEUE_HIGH_FRAMES) {
            u64 wait_start = SDL_GetTicks64();

            while (audio_queued_frames() > AUDIO_QUEUE_HIGH_FRAMES) {
                if (!idle(user))
                    return false;
                if (SDL_GetTicks64() - wait_start > 3000)
                    break;
                svcSleepThread(5 * 1000 * 1000ULL);
            }
        }
    }
}

/* An audio problem must never stop the picture: drop the audio track and
 * continue on the wall clock. */
static void disable_audio(Media *m)
{
    if (m->actx != NULL)
        avcodec_free_context(&m->actx);
    if (m->swr != NULL)
        swr_free(&m->swr);

    m->as = -1;
    audio_use_wallclock(true);
}

/* ------------------------------------------------------------------------- */
/* Playback                                                                  */
/* ------------------------------------------------------------------------- */

static void rewind_media(Media *m)
{
    av_seek_frame(m->fmt, -1, 0, AVSEEK_FLAG_BACKWARD);

    avcodec_flush_buffers(m->vctx);
    if (m->actx != NULL)
        avcodec_flush_buffers(m->actx);
    if (m->swr != NULL)
        swr_init(m->swr); /* drops any buffered samples */

    av_frame_unref(m->frame);
    m->last_pts = 0.0;
}

bool media_first_frame(Media *m)
{
    /* Silence the device: nothing must be heard during the two seconds the
     * first picture is held on screen. */
    audio_start();

    rewind_media(m);

    for (;;) {
        int ret = av_read_frame(m->fmt, m->pkt);
        bool shown = false;

        if (ret < 0)
            return false;

        if (m->pkt->stream_index == m->vs) {
            ret = avcodec_send_packet(m->vctx, m->pkt);
            if (ret >= 0 || ret == AVERROR(EAGAIN)) {
                if (avcodec_receive_frame(m->vctx, m->frame) == 0) {
                    m->last_pts = 0.0;
                    shown = convert_and_show(m, m->frame);
                    av_frame_unref(m->frame);
                }
            }
        }

        av_packet_unref(m->pkt);

        if (shown) {
            video_present();
            return true;
        }
    }
}

bool media_play(Media *m, MediaIdleFn idle, void *user)
{
    rewind_media(m);

    audio_start();
    audio_use_wallclock(m->as < 0 || !audio_available());

    for (;;) {
        int ret;
        bool ok = true;

        if (!idle(user))
            return false;

        ret = av_read_frame(m->fmt, m->pkt);

        if (ret == AVERROR_EOF)
            break;
        if (ret < 0)
            break; /* unreadable data: treat as the end of the file */

        if (m->pkt->stream_index == m->vs) {
            if (avcodec_send_packet(m->vctx, m->pkt) >= 0)
                ok = drain_video(m, idle, user, true);
        } else if (m->actx != NULL && m->pkt->stream_index == m->as) {
            if (avcodec_send_packet(m->actx, m->pkt) >= 0) {
                ok = drain_audio(m, idle, user);
                if (!ok) {
                    ok = true;
                    disable_audio(m);
                }
            }
        }

        av_packet_unref(m->pkt);

        if (!ok) {
            if (!idle(user))
                return false; /* the user pressed "+" */
            break;            /* video decode error: restart the loop */
        }
    }

    /* Flush the decoders so the tail of the file is played too. */
    if (avcodec_send_packet(m->vctx, NULL) >= 0) {
        if (!drain_video(m, idle, user, true) && !idle(user))
            return false;
    }

    if (m->actx != NULL) {
        if (avcodec_send_packet(m->actx, NULL) >= 0) {
            if (!drain_audio(m, idle, user) && !idle(user))
                return false;
        }

        if (m->swr != NULL && m->pcm != NULL) {
            uint8_t *out[1];
            int got;

            out[0] = (uint8_t *)m->pcm;
            got = swr_convert(m->swr, out, m->pcm_frames, NULL, 0);
            if (got > 0)
                audio_push(m->pcm, got);
        }
    }

    /* Let the last samples reach the speakers before the loop restarts. */
    {
        u64 start = SDL_GetTicks64();

        while (audio_queued_frames() > 0) {
            if (!idle(user))
                return false;
            if (SDL_GetTicks64() - start > 5000)
                break;
            svcSleepThread(10 * 1000 * 1000ULL);
        }
    }

    return true;
}
