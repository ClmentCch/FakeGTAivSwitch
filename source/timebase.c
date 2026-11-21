/*
 * timebase.c - monotonic millisecond clock.
 */

#include "timebase.h"

#include <switch.h>

uint64_t time_ms(void)
{
    return armTicksToNs(armGetSystemTick()) / 1000000ULL;
}
