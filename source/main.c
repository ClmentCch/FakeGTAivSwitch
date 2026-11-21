/*
 * main.c - Grand Theft Auto VI, Nintendo Switch homebrew video player.
 *
 * Plays sdmc:/switch/gtaVI/video.mp4 full screen with sound, in an endless
 * loop. Every loop shows the first picture for two seconds before playback
 * (with audio) starts again. Press "+" to quit.
 */

#include <switch.h>
#include <SDL2/SDL.h>

#include <stdio.h>
#include <stdlib.h>

#include "audio.h"
#include "media.h"
#include "video.h"

/* Time the first frame stays on screen before each playback. */
#define FIRST_FRAME_HOLD_MS 2000u

static PadState      s_pad;
static volatile bool s_quit;

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
    int rc = EXIT_FAILURE;

    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0)
        return EXIT_FAILURE;

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&s_pad);

    fsInitialize();
    if (R_FAILED(fsdevMountSdmc())) {
        printf("gtaVI: cannot mount the SD card\n");
        goto cleanup;
    }

    if (!video_init()) {
        printf("gtaVI: cannot create the video output\n");
        goto cleanup;
    }

    if (!audio_init()) {
        printf("gtaVI: cannot open the audio device\n");
        goto cleanup;
    }

    media = media_open(VIDEO_PATH);
    if (media == NULL) {
        printf("gtaVI: cannot open or decode %s\n", VIDEO_PATH);
        goto cleanup;
    }

    while (!s_quit) {
        if (!media_first_frame(media)) {
            printf("gtaVI: cannot decode the first video frame\n");
            break;
        }

        hold_first_frame();

        if (s_quit)
            break;

        if (!media_play(media, pump_events, NULL))
            break;
    }

    rc = EXIT_SUCCESS;

cleanup:
    media_close(media);
    audio_exit();
    video_exit();
    SDL_Quit();
    fsdevUnmountDevice("sdmc");
    fsExit();

    return rc;
}
