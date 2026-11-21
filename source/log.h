/*
 * log.h - diagnostic log written next to the NRO on the SD card.
 *
 * The log is invisible on screen (the player must never show any interface),
 * but it records what happened during startup so a failure can be understood
 * by reading sdmc:/switch/gtaVI/gtaVI.log after a crash.
 */

#pragma once

/* Opens (truncates) the log file. Must be called after the SD card is
 * mounted; does nothing when the file cannot be created. */
void log_init(void);

/* Appends one line and flushes it immediately. */
void log_line(const char *fmt, ...);

void log_close(void);
