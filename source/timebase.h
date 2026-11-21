/*
 * timebase.h - monotonic millisecond clock.
 */

#pragma once

#include <stdint.h>

/* Milliseconds elapsed since the console booted. */
uint64_t time_ms(void);
