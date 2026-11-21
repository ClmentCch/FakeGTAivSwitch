/*
 * media.c - MP4 demuxing, decoding and playback.
 *
 * The file is opened with libnx's stdio (newlib devoptab) and handed to
 * FFmpeg through a custom AVIOContext. This is required because FFmpeg parses
 * "sdmc:/..." as a protocol named "sdmc", which does not exist in the file
 * protocol set.
 *
 * libavformat reads the container, libavcodec decodes the video and audio
 * streams, libswscale converts the picture straight into the console
 * framebuffer (scaled and letterboxed) and libswresample converts the audio to
 * the stereo s16 stream the audio device expects.
 *
 * The player runs on a single thread: the demuxer is read packet by packet,
 * audio is pushed to the device (which is also the master clock) and video
 * frames are presented when their timestamp is reached on that clock.
 */

#include "media.h"

#include "audio.h"
#include "log.h"
#include "timebase.h"
#include "video.h"

#include <switch.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Audio is kept at most half a second ahead of the device. */
#define AUDIO_QUEUE_HIGH_FRAMES 24000

/* Frames arriving later than this are dropped to keep sound and picture
 * together. */
#define VIDEO_LATE_TOLERANCE 0.045

/* Small head start that compensates the presentation latency. */
#define PRESENT_LEAD 0.008

/* A decoder that cannot keep up must not freeze the screen: late frames are
 * dropped, but never more than this many in a row. */
#define MAX_DROP_STREAK 4

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
    AVPacket        *pkt;

    int     vs;
    int     as;
    double  vtb;
    int64_t vstart;

    /* Geometry the current scaling context was built for. */
    int vw, vh;
    int dw, dh;
    int src_fmt;

    int16_t *pcm;
    int      pcm_frames;

    double last_pts;

    int      late_streak;
    unsigned shown;
    unsigned dropped;
    uint64_t stat_ms;
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
/* Logging                                                                   */
/* ------------------------------------------------------------------------- */

/* FFmpeg messages are forwarded to the SD log: they are invisible on screen
 * but tell exactly why a file could not be opened or decoded. */
static void ffmpeg_log_cb(void *avcl, int level, const char *fmt, va_list vl)
{
    char buf[512];
    size_t len;

    (void)avcl;

    if (level > AV_LOG_WARNING)
        return;

    vsnprintf(buf, sizeof(buf), fmt, vl);

    len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
        buf[--len] = '\0';

    log_line("[ffmpeg] %s", buf);
}

/* ------------------------------------------------------------------------- */
/* Opening / closing                                                         */
/* ------------------------------------------------------------------------- */

static AVCodecContext *open_decoder(AVFormatContext *fmt, int index, bool is_video)
{
    AVStream *stream = fmt->streams[index];
    const AVCodec *codec = NULL;
    AVCodecContext *ctx;
    int ret;

    /* dav1d decodes AV1 far faster than FFmpeg's built-in decoder. */
    if (is_video && stream->codecpar->codec_id == AV_CODEC_ID_AV1)
        codec = avcodec_find_decoder_by_name("libdav1d");

    if (codec == NULL)
        codec = avcodec_find_decoder(stream->codecpar->codec_id);

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

    if (is_video) {
        ctx->thread_count = 4; /* bounded memory on the console */

        /* A source larger than the screen cannot be decoded in real time in
         * software on this hardware: trade a little quality (deblocking) for a
         * frame rate that still looks like video instead of a slideshow. */
        if (stream->codecpar->width > 1920 || stream->codecpar->height > 1080) {
            ctx->flags2 |= AV_CODEC_FLAG2_FAST;
            ctx->skip_loop_filter = AVDISCARD_ALL;
            /* Decoding only the reference frames halves the work on a source
             * that cannot be shown at full rate anyway, and keeps the picture
             * moving smoothly instead of dropping almost everything. */
            ctx->skip_frame = AVDISCARD_NONREF;
        }
    }

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
    m->src_fmt = -1;

    av_log_set_level(AV_LOG_WARNING);
    av_log_set_callback(ffmpeg_log_cb);

    m->file = fopen(path, "rb");
    if (m->file == NULL) {
        log_line("cannot open %s: %s", path, strerror(errno));
        goto fail;
    }

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

    if (avformat_open_input(&m->fmt, NULL, NULL, NULL) < 0) {
        log_line("avformat_open_input failed");
        goto fail;
    }

    if (avformat_find_stream_info(m->fmt, NULL) < 0) {
        log_line("avformat_find_stream_info failed");
        goto fail;
    }

    m->vs = av_find_best_stream(m->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (m->vs < 0) {
        log_line("no video stream found");
        goto fail;
    }

    m->as = av_find_best_stream(m->fmt, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);

    m->vctx = open_decoder(m->fmt, m->vs, true);
    if (m->vctx == NULL) {
        log_line("no decoder for video stream %d (%s)", m->vs,
                 avcodec_get_name(m->fmt->streams[m->vs]->codecpar->codec_id));
        goto fail;
    }

    if (m->as >= 0) {
        m->actx = open_decoder(m->fmt, m->as, false);
        if (m->actx == NULL) {
            log_line("no decoder for audio stream %d, playing silently", m->as);
            m->as = -1; /* play silently instead of failing */
        }
    }

    vstream = m->fmt->streams[m->vs];
    m->vtb = av_q2d(vstream->time_base);
    m->vstart = vstream->start_time == AV_NOPTS_VALUE ? 0 : vstream->start_time;

    m->pkt = av_packet_alloc();
    m->frame = av_frame_alloc();
    if (m->pkt == NULL || m->frame == NULL)
        goto fail;

    log_line("media: %dx%d %s (decoder %s), %.2f fps, %.1f s, threads %d, audio %d",
             m->vctx->width, m->vctx->height,
             avcodec_get_name(m->vctx->codec_id),
             m->vctx->codec != NULL ? m->vctx->codec->name : "?",
             vstream->avg_frame_rate.num != 0 ? av_q2d(vstream->avg_frame_rate) : 0.0,
             m->fmt->duration > 0 ? (double)m->fmt->duration / (double)AV_TIME_BASE : 0.0,
             m->vctx->thread_count, m->as);

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

/* Decodes one picture directly into the framebuffer, scaled to fit the screen
 * with black letterbox bars. */
static bool convert_and_show(Media *m, AVFrame *frame)
{
    int out_w = 0;
    int out_h = 0;
    int dst_w;
    int dst_h;
    int off_x;
    int off_y;
    double scale;
    u32 stride = 0;
    uint8_t *base;
    uint8_t *dst_data[4];
    int dst_linesize[4];

    video_output_size(&out_w, &out_h);

    scale = (double)out_w / (double)frame->width;
    if ((double)out_h / (double)frame->height < scale)
        scale = (double)out_h / (double)frame->height;

    dst_w = (int)((double)frame->width * scale);
    dst_h = (int)((double)frame->height * scale);
    if (dst_w < 2)
        dst_w = 2;
    if (dst_h < 2)
        dst_h = 2;
    if (dst_w > out_w)
        dst_w = out_w;
    if (dst_h > out_h)
        dst_h = out_h;

    off_x = (out_w - dst_w) / 2;
    off_y = (out_h - dst_h) / 2;

    if (m->sws == NULL || m->vw != frame->width || m->vh != frame->height ||
        m->dw != dst_w || m->dh != dst_h || m->src_fmt != frame->format) {
        if (m->sws != NULL)
            sws_freeContext(m->sws);

        m->sws = sws_getContext(frame->width, frame->height,
                                (enum AVPixelFormat)frame->format,
                                dst_w, dst_h, AV_PIX_FMT_RGBA,
                                SWS_FAST_BILINEAR, NULL, NULL, NULL);
        if (m->sws == NULL)
            return false;

        m->vw = frame->width;
        m->vh = frame->height;
        m->dw = dst_w;
        m->dh = dst_h;
        m->src_fmt = frame->format;
    }

    base = video_begin_frame(&stride);
    if (base == NULL)
        return false;

    /* Only the letterbox area needs clearing: when the picture fills the
     * screen the scaling pass overwrites every pixel. */
    if (off_y > 0 || off_x > 0 || dst_w < out_w || dst_h < out_h)
        memset(base, 0, (size_t)stride * (size_t)out_h);

    dst_data[0] = base + (size_t)off_y * (size_t)stride + (size_t)off_x * 4u;
    dst_data[1] = NULL;
    dst_data[2] = NULL;
    dst_data[3] = NULL;

    dst_linesize[0] = (int)stride;
    dst_linesize[1] = 0;
    dst_linesize[2] = 0;
    dst_linesize[3] = 0;

    sws_scale(m->sws,
              (const uint8_t * const *)frame->data, frame->linesize, 0, frame->height,
              dst_data, dst_linesize);

    video_end_frame();
    return true;
}

static bool show_frame(Media *m, AVFrame *frame, MediaIdleFn idle, void *user, bool timed)
{
    double pts = frame_pts(m, frame);

    if (timed) {
        uint64_t wait_start = time_ms();

        /* Late frames are dropped to keep sound and picture together, but a
         * decoder slower than real time must not leave the screen frozen on
         * one picture: after a few drops the next frame is shown anyway. */
        if (audio_clock() - pts > VIDEO_LATE_TOLERANCE && m->late_streak < MAX_DROP_STREAK) {
            m->late_streak++;
            m->dropped++;
            audio_pump();
            return true;
        }

        for (;;) {
            double now = audio_clock();
            double wait;

            if (now + PRESENT_LEAD >= pts)
                break;

            /* Never block on a clock that stopped moving (for example when
             * the first audio samples have not been decoded yet). */
            if (time_ms() - wait_start > 150)
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

    m->late_streak = 0;
    m->shown++;

    return convert_and_show(m, frame);
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

        out_max = (int)av_rescale_rnd(swr_get_delay(m->swr, m->actx->sample_rate) +
                                      m->frame->nb_samples,
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

        audio_pump();

        /* Back pressure: never let the queue grow without bound. The wait is
         * bounded so a dead audio device can never block the player. */
        if (audio_queued_frames() > AUDIO_QUEUE_HIGH_FRAMES) {
            uint64_t wait_start = time_ms();

            while (audio_queued_frames() > AUDIO_QUEUE_HIGH_FRAMES) {
                if (!idle(user))
                    return false;
                if (time_ms() - wait_start > 3000)
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

        if (shown)
            return true;
    }
}

/* Decodes and presents the first picture of the loop. Audio packets read on
 * the way are skipped: the audio timeline starts once the picture is on
 * screen, otherwise the sound immediately runs ahead of a frame that has not
 * been decoded yet and every following frame looks late. */
static bool first_frame_of_loop(Media *m, MediaIdleFn idle, void *user)
{
    for (;;) {
        int ret;
        bool shown = false;

        if (!idle(user))
            return false;

        ret = av_read_frame(m->fmt, m->pkt);
        if (ret < 0)
            return false;

        if (m->pkt->stream_index == m->vs) {
            ret = avcodec_send_packet(m->vctx, m->pkt);
            if (ret >= 0 || ret == AVERROR(EAGAIN)) {
                if (avcodec_receive_frame(m->vctx, m->frame) == 0) {
                    shown = convert_and_show(m, m->frame);
                    av_frame_unref(m->frame);
                }
            }
        }

        av_packet_unref(m->pkt);

        if (shown)
            return true;
    }
}

bool media_play(Media *m, MediaIdleFn idle, void *user)
{
    rewind_media(m);

    m->shown = 0;
    m->dropped = 0;
    m->late_streak = 0;
    m->stat_ms = time_ms();

    if (!first_frame_of_loop(m, idle, user)) {
        if (!idle(user))
            return false;
    }

    audio_start();
    audio_use_wallclock(m->as < 0 || !audio_available());

    for (;;) {
        int ret;
        bool ok = true;

        if (!idle(user))
            return false;

        audio_pump();

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

        if (time_ms() - m->stat_ms >= 2000) {
            m->stat_ms = time_ms();
            log_line("playing: %u shown, %u dropped, clock %.1fs",
                     m->shown, m->dropped, audio_clock());
        }

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
        uint64_t start = time_ms();

        while (audio_queued_frames() > 0) {
            if (!idle(user))
                return false;
            audio_pump();
            if (time_ms() - start > 5000)
                break;
            svcSleepThread(10 * 1000 * 1000ULL);
        }
    }

    log_line("loop end: %u shown, %u dropped", m->shown, m->dropped);

    return true;
}
