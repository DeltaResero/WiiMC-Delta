/*
 * Platform functions Mbed TLS needs on the Wii: a millisecond clock
 * (MBEDTLS_PLATFORM_MS_TIME_ALT) and an entropy source
 * (MBEDTLS_ENTROPY_HARDWARE_ALT).
 *
 * Entropy:
 * The Wii gives the PowerPC no random number generator that libogc
 * exposes. This collects timing jitter instead: each sample is the length,
 * in PowerPC timebase ticks, of an IPC round trip to the Starlet, the ARM
 * processor that runs IOS on its own clock. Only the low bits of each
 * length vary, so every output byte folds together eight samples. On the
 * first call the console's device ID and the clock are XORed in as well.
 * Mbed TLS hashes the result into its entropy pool.
 *
 * How much entropy this yields has not been measured. It is weaker than a
 * hardware generator, and an observer who can time the console closely
 * could narrow it down.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <string.h>
#include <time.h>
#include <gccore.h>
#include <ogc/es.h>
#include <ogc/lwp_watchdog.h>

#include "mbedtls/entropy.h"
#include "mbedtls/platform_time.h"

/* Milliseconds from the PowerPC timebase, which counts from power-on. */
mbedtls_ms_time_t mbedtls_ms_time(void)
{
    return (mbedtls_ms_time_t)ticks_to_millisecs(gettime());
}

#define SAMPLES_PER_BYTE 8

static unsigned char jitter_byte(void)
{
    unsigned char b = 0;
    u32 id;
    int i;

    for (i = 0; i < SAMPLES_PER_BYTE; i++) {
        u64 t0 = gettime();
        ES_GetDeviceID(&id);
        u64 dt = diff_ticks(t0, gettime());
        // rotate so each sample's low bits land on different bit positions
        b = (unsigned char)((b << 3) | (b >> 5));
        b ^= (unsigned char)(dt ^ (dt >> 8));
    }
    return b;
}

int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen)
{
    static int first = 1;
    size_t n;

    (void)data;

    for (n = 0; n < len; n++)
        output[n] = jitter_byte();

    if (first && len) {
        /* Not secret, but different between consoles and boots. XORed in
         * rather than returned on their own, so they never count as
         * entropy by themselves. */
        unsigned char extra[sizeof(u32) + sizeof(u64) + sizeof(time_t)];
        u32 id = 0;
        u64 tb = gettime();
        time_t now = time(NULL);

        first = 0;
        ES_GetDeviceID(&id);
        memcpy(extra, &id, sizeof(id));
        memcpy(extra + sizeof(id), &tb, sizeof(tb));
        memcpy(extra + sizeof(id) + sizeof(tb), &now, sizeof(now));
        for (n = 0; n < sizeof(extra); n++)
            output[n % len] ^= extra[n];
    }

    *olen = len;
    return 0;
}
