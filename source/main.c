/*
 * main.c - Grand Theft Auto VI, Nintendo Switch homebrew video player.
 *
 * Plays sdmc:/switch/gtaVI/video.mp4 full screen with sound, in an endless
 * loop. Every loop shows the first picture for two seconds before playback
 * (with audio) starts again. Press "+" to quit.
 *
 * Only libnx is used for the console itself (framebuffer, pad, audout); FFmpeg
 * does the decoding. Startup order matters: the filesystem is mounted first so
 * the log file can be written, the display is required, and audio is optional
 * (a console that refuses an audio device still plays the video, silently).
 */

#include <switch.h>

#include <stdio.h>
#include <stdlib.h>

#include "audio.h"
#include "log.h"
#include "player.h"
#include "timebase.h"
#include "gfx.h"

/* Time the first frame stays on screen before each playback. */
#define FIRST_FRAME_HOLD_MS 2000u

static PadState      s_pad;
static volatile bool s_quit;

/* Polls the controller and keeps the audio device fed. Returns false when the
 * user pressed "+", which stops the player. */
static bool pump_events(void *user)
{
    (void)user;

    padUpdate(&s_pad);
    if ((padGetButtonsDown(&s_pad) & HidNpadButton_Plus) != 0)
        s_quit = true;

    audio_pump();

    return !s_quit;
}

/* Keeps the first frame on screen without any interface for two seconds. */
static void hold_first_frame(void)
{
    uint64_t start = time_ms();

    while (!s_quit && time_ms() - start < FIRST_FRAME_HOLD_MS) {
        pump_events(NULL);
        svcSleepThread(10 * 1000 * 1000ULL);
    }
}

int main(int argc, char **argv)
{
    Player *media = NULL;
    unsigned loop = 0;
    int rc = EXIT_FAILURE;

    (void)argc;
    (void)argv;

    /* hbmenu and sphaira already mount the SD card inside the process that
     * runs this NRO, so the "sdmc" device normally exists before main() is
     * reached. Mounting it again fails (the devoptab name is taken) and an
     * unconditional failure check here used to end the program before it could
     * draw anything or write its log: that was the black screen followed by an
     * immediate return to the Homebrew Menu. Only mount when it is missing. */
    if (fsdevGetDeviceFileSystem("sdmc") == NULL) {
        if (R_FAILED(fsInitialize()) || R_FAILED(fsdevMountSdmc()))
            return EXIT_FAILURE;
    }

    log_init();
    log_line("gtaVI " APP_VERSION " - starting");

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&s_pad);

    if (!gfx_init())
        goto cleanup;

    if (audio_init())
        log_line("audio ready at %d Hz", audio_rate());
    else
        log_line("no audio device, playing silently");

    if (R_SUCCEEDED(appletSetAutoSleepDisabled(true)))
        log_line("auto sleep disabled");

    media = player_open(VIDEO_PATH);
    if (media == NULL)
        goto cleanup;

    while (!s_quit) {
        loop++;

        if (!player_first_frame(media)) {
            log_line("loop %u: cannot decode the first frame", loop);
            break;
        }

        hold_first_frame();

        if (s_quit)
            break;

        log_line("loop %u: playing", loop);

        if (!player_play(media, pump_events, NULL))
            break;
    }

    log_line("stopping (loops=%u, quit=%d)", loop, s_quit ? 1 : 0);
    rc = EXIT_SUCCESS;

cleanup:
    player_close(media);
    audio_exit();
    gfx_exit();

    appletSetAutoSleepDisabled(false);

    log_line("bye");
    log_close();

    /* The file system and the SD card device belong to the launcher: they are
     * deliberately not unmounted or closed, so hbmenu keeps working after the
     * player exits. */

    return rc;
}
