/*
 * log.c - diagnostic log written next to the NRO on the SD card.
 */

#include "log.h"

#include <stdarg.h>
#include <stdio.h>

#define LOG_PATH "sdmc:/switch/gtaVI/gtaVI.log"

static FILE *s_file;

void log_init(void)
{
    s_file = fopen(LOG_PATH, "w");
    if (s_file != NULL) {
        /* Unbuffered: every line must already be on the card when a crash
         * kills the process. */
        setvbuf(s_file, NULL, _IONBF, 0);
    }
}

void log_line(const char *fmt, ...)
{
    va_list ap;

    if (s_file == NULL)
        return;

    va_start(ap, fmt);
    vfprintf(s_file, fmt, ap);
    va_end(ap);

    fputc('\n', s_file);
}

void log_close(void)
{
    if (s_file != NULL) {
        fclose(s_file);
        s_file = NULL;
    }
}
