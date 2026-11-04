/****************************************************************************
 * WiiMC
 *
 * netdb_wii.c
 *
 * libogc's netdb.h declares gethostbyaddr(), but its socket layer has no
 * reverse DNS lookup. FFmpeg's getnameinfo() fallback calls it and prints
 * the numeric address when it fails, which is all WiiMC needs.
 ***************************************************************************/

#include <stddef.h>
#include <netdb.h>

struct hostent *gethostbyaddr(const void *addr, socklen_t len, int type)
{
    (void)addr;
    (void)len;
    (void)type;
    return NULL;
}
