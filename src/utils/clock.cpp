/****************************************************************************
 * WiiMC
 *
 * clock.cpp
 *
 * WiiMC's time of day, corrected over the internet when the Wii's clock is
 * wrong. https needs it: a server's certificate counts as valid only
 * between two dates, so a clock that's days off (or back in 2000 after the
 * clock battery died) turns every https server away.
 *
 * Only WiiMC's own time changes, never the Wii's clock: the DOL is linked
 * with --wrap for libogc's two time-of-day calls, and they add an offset.
 * The timers keep counting from the Wii's clock as before.
 ***************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <sys/time.h>
#include <sys/reent.h>
#include <gccore.h>

#include "clock.h"
#include "http.h"

// a server made to answer plain http, with nothing to send but its headers
#define CLOCK_URL "http://connectivitycheck.gstatic.com/generate_204"

static volatile time_t offset = 0; // added to the Wii's time

extern "C" {
int __real___syscall_gettod_r(struct _reent *ptr, struct timeval *tp, struct timezone *tz);
int __wrap___syscall_gettod_r(struct _reent *ptr, struct timeval *tp, struct timezone *tz);
int __real___syscall_clock_gettime(clockid_t clock_id, struct timespec *tp);
int __wrap___syscall_clock_gettime(clockid_t clock_id, struct timespec *tp);

int __wrap___syscall_gettod_r(struct _reent *ptr, struct timeval *tp, struct timezone *tz)
{
    int ret = __real___syscall_gettod_r(ptr, tp, tz);

    if (ret == 0 && tp)
        tp->tv_sec += offset;
    return ret;
}

// the monotonic clock, which timers use, stays as it is
int __wrap___syscall_clock_gettime(clockid_t clock_id, struct timespec *tp)
{
    int ret = __real___syscall_clock_gettime(clock_id, tp);

    if (ret == 0 && tp && clock_id == CLOCK_REALTIME)
        tp->tv_sec += offset;
    return ret;
}
}

void ClockCorrectOnline()
{
    time_t utc = http_server_time(CLOCK_URL);
    if (utc <= 0)
    {
        printf("clock: no time from %s\n", CLOCK_URL);
        return;
    }

    // The Wii's clock keeps the local time, which libogc gives as if it
    // were UTC. Take what it's off by, less a whole number of days, as its
    // time zone, to the nearest quarter hour, and fix only the rest: the
    // days and the minutes. A right clock in any time zone stays as it is.
    long long diff = (long long)(time(NULL) - offset) - utc;
    long long day = 86400;
    long long zone = ((diff % day) + day) % day; // 0 to a day
    if (zone > 14 * 3600)
        zone -= day; // UTC-10 to UTC+14
    zone = (zone + (zone < 0 ? -450 : 450)) / 900 * 900;

    long long wrong = diff - zone;
    if (wrong > -120 && wrong < 120)
    {
        printf("clock: the Wii's clock is right (UTC%+lld min)\n", zone / 60);
        return;
    }
    offset = (time_t)-wrong;
    printf("clock: the Wii's clock is %lld s off; WiiMC uses the internet's time\n", wrong);
}
