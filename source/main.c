/*
 * main.c - Grand Theft Auto VI, Nintendo Switch homebrew video player.
 *
 * Plays sdmc:/switch/gtaVI/video.mp4 full screen with sound, in an endless
 * loop. Every loop shows the first picture for two seconds before playback
 * (with audio) starts again. Press "+" to quit.
 *
 * Startup order matters:
 *   1. the filesystem is mounted first, so the log file can be written;
 *   2. video is initialised on its own and is required;
 *   3. audio is optional: when the console refuses an audio device (for
 *      example in applet mode), the video is still played, silently.
 */

#include <switch.h>
#include <SDL2/SDL.h>

#include <stdio.h>
#include <stdlib.h>

#include "audio.h"
#include "log.h"
#include "media.h"
#include "video.h"

/* Time the first frame stays on screen before each playback. */
#define FIRST_FRAME_HOLD_MS 2000u

static PadState       s_pad;
static volatile bool  s_quit;

/* Pumps SDL events and the controllers. Returns false when the user pressed
 * "+" (or closed the window), which stops the player. */
static bool pump_events(void *user)
{
    SDL_Event event;

    (void)user;

    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT)
            s_quit = true;
    }

    padUpdate(&s_pad);
    if ((padGetButtonsDown(&s_pad) & HidNpadButton_Plus) != 0)
        s_quit = true;

    return !s_quit;
}

/* Keeps the first frame on screen without any interface for two seconds. */
static void hold_first_frame(void)
{
    u64 start = SDL_GetTicks64();

    while (!s_quit && SDL_GetTicks64() - start < FIRST_FRAME_HOLD_MS) {
        pump_events(NULL);
        svcSleepThread(20 * 1000 * 1000ULL);
    }
}

int main(int argc, char **argv)
{
    Media *media = NULL;
    unsigned loop = 0;
    int rc = EXIT_FAILURE;

    (void)argc;
    (void)argv;

    /* The log lives on the SD card, so it must be mounted first. */
    fsInitialize();
    if (R_FAILED(fsdevMountSdmc()))
        return EXIT_FAILURE;

    log_init();
    log_line("gtaVI " APP_VERSION " - starting");

    /* Video (and the timer used for the two second hold) is mandatory. */
    if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        log_line("SDL video init failed: %s", SDL_GetError());
        goto cleanup;
    }

    {
        const char *driver = SDL_GetCurrentVideoDriver();
        log_line("SDL %d.%d.%d, video driver: %s",
                 SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL,
                 driver != NULL ? driver : "?");
    }

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&s_pad);

    if (!video_init()) {
        log_line("video output failed: %s", SDL_GetError());
        goto cleanup;
    }
    log_line("video output ready");

    /* Audio is a bonus: the console (especially in applet mode) may refuse to
     * give an audio device, which must not prevent the video from playing. */
    if (audio_init())
        log_line("audio ready at %d Hz", audio_rate());
    else
        log_line("no audio device (%s), playing silently", SDL_GetError());

    if (R_SUCCEEDED(appletSetAutoSleepDisabled(true)))
        log_line("auto sleep disabled");

    media = media_open(VIDEO_PATH);
    if (media == NULL)
        goto cleanup;

    while (!s_quit) {
        loop++;

        if (!media_first_frame(media)) {
            log_line("loop %u: cannot decode the first frame", loop);
            break;
        }

        hold_first_frame();

        if (s_quit)
            break;

        log_line("loop %u: playing", loop);

        if (!media_play(media, pump_events, NULL))
            break;
    }

    log_line("stopping (loops=%u, quit=%d)", loop, s_quit ? 1 : 0);
    rc = EXIT_SUCCESS;

cleanup:
    media_close(media);
    audio_exit();
    video_exit();

    appletSetAutoSleepDisabled(false);
    SDL_Quit();

    log_line("bye");
    log_close();

    fsdevUnmountDevice("sdmc");
    fsExit();

    return rc;
}
