/*
 * HLS: a playlist of MPEG-TS pieces, read one after another as one stream,
 * so that MPlayer's cache can fetch ahead of playback. A live stream's
 * playlist grows as the stream goes on, and is read again for new pieces.
 *
 * Pieces in MPEG-TS only: no encryption, byte ranges or fragmented MP4.
 *
 * This file is part of MPlayer.
 *
 * MPlayer is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * MPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with MPlayer; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libavformat/avio.h"
#include "libavutil/avstring.h"
#include "libavutil/error.h"
#include "libavutil/mem.h"
#include "libavutil/time.h"
#include "mp_msg.h"
#include "stream_hls.h"

#ifdef GEKKO
#include <ogc/lwp.h>
extern int stop_cache_thread; /* cache2.c */
#endif

#define HLS_PIECES_BYTES (1024 * 1024) /* the addresses kept, newest last: a
                                          live stream that can be wound back
                                          for hours lists thousands of them */
#define HLS_LINE_MAX 8192
#define HLS_LIVE_START 6     /* how many pieces from its end a live stream starts:
                                the further back, the more the cache holds */
#define HLS_TALLEST 480      /* the tallest picture picked from a list of sizes */
#define HLS_MAX_FAILURES 10  /* failed reads in a row before giving up */

struct hls {
    char *url;                    /* the playlist of pieces */
    AVDictionary *opts;           /* for every connection */
    AVIOInterruptCB icb;
#ifdef GEKKO
    lwp_t opener;                 /* the thread that opened the stream */
#endif
    char **piece;                 /* the pieces listed, from first on */
    int pieces;
    int room;                     /* how many piece has room for */
    size_t held;                  /* the bytes their addresses take */
    int made;                     /* the last piece's address was made here */
    int relist;                   /* a made address failed: read the list again */
    int64_t first;                /* the sequence number of piece[0] */
    int64_t next;                 /* the sequence number of the piece to read next */
    int target;                   /* the longest a piece lasts, in seconds */
    int ended;                    /* the list is complete: no new pieces will come */
    int64_t loaded;               /* when the list was last read (av_gettime_relative) */
    int failures;
    AVIOContext *pb;              /* the piece being read */
};

int hls_is_playlist(const char *url, const char *mime)
{
    const char *q = strchr(url, '?');
    size_t n = q ? (size_t)(q - url) : strlen(url);

    if (mime && (!av_strcasecmp(mime, "application/vnd.apple.mpegurl") ||
                 !av_strcasecmp(mime, "application/x-mpegurl")))
        return 1;
    return n >= 5 && !av_strncasecmp(url + n - 5, ".m3u8", 5);
}

/* Playback is stopping and this isn't the thread that opened the stream:
 * the cache thread, which must let go within the 2 s cache_uninit waits. */
static int stopping(void *opaque)
{
#ifdef GEKKO
    hls_t *h = opaque;
    return stop_cache_thread && LWP_GetSelf() != h->opener;
#else
    return 0;
#endif
}

/* Sleeps until the time given (av_gettime_relative); -1 if playback stops. */
static int wait_until(hls_t *h, int64_t until)
{
    while (av_gettime_relative() < until) {
        if (stopping(h))
            return -1;
        av_usleep(100000);
    }
    return 0;
}

/* rel made whole against base, the address of the playlist it's in */
static char *make_url(const char *base, const char *rel)
{
    const char *end;

    if (strstr(rel, "://"))
        return av_strdup(rel);
    if (rel[0] == '/') {
        /* from the server's root: keep scheme://host */
        const char *p = strstr(base, "://");
        end = p ? strchr(p + 3, '/') : NULL;
        if (!end)
            end = base + strlen(base);
    } else {
        /* from the playlist's folder */
        const char *q = strchr(base, '?');
        end = q ? q : base + strlen(base);
        while (end > base && end[-1] != '/')
            end--;
    }
    return av_asprintf("%.*s%s", (int)(end - base), base, rel);
}

static void forget_pieces(hls_t *h)
{
    int i;

    for (i = 0; i < h->pieces; i++)
        av_freep(&h->piece[i]);
    h->pieces = 0;
    h->held = 0;
    h->made = 0;
}

/* Adds a piece's address, which becomes the list's. The oldest go once
 * the addresses take more than HLS_PIECES_BYTES. */
static void add_piece(hls_t *h, char *u)
{
    size_t len = strlen(u) + 1;

    if (h->pieces == h->room) {
        char **p = av_realloc_array(h->piece, h->room + 256, sizeof(*p));
        if (!p) {
            av_free(u);
            return;
        }
        h->piece = p;
        h->room += 256;
    }
    while (h->pieces > 0 && h->held + len > HLS_PIECES_BYTES) {
        h->held -= strlen(h->piece[0]) + 1;
        av_free(h->piece[0]);
        memmove(h->piece, h->piece + 1, --h->pieces * sizeof(*h->piece));
        h->first++;
    }
    h->piece[h->pieces++] = u;
    h->held += len;
}

/* The address of the piece after the last one listed, for a list whose
 * addresses differ only in the piece's number, as YouTube's do: /sq/N/.
 * Its server keeps a request for a piece that's about to exist waiting
 * until it does, so the list needn't be read again. NULL for other lists. */
static char *next_numbered(hls_t *h)
{
    char number[40];
    const char *last, *p;
    int64_t seq = h->first + h->pieces - 1;

    if (!h->pieces)
        return NULL;
    last = h->piece[h->pieces - 1];
    snprintf(number, sizeof(number), "/sq/%"PRId64"/", seq);
    p = strstr(last, number);
    if (!p)
        return NULL;
    return av_asprintf("%.*s/sq/%"PRId64"/%s", (int)(p - last), last, seq + 1, p + strlen(number));
}

/* The next line of a playlist, without its line end; -1 at the end. */
static int next_line(AVIOContext *pb, char *line, int size)
{
    int n = 0, any = 0;

    for (;;) {
        int c = avio_r8(pb);
        if (!c && avio_feof(pb))
            break;
        any = 1;
        if (c == '\n')
            break;
        if (c != '\r' && n < size - 1)
            line[n++] = c;
    }
    line[n] = 0;
    return any ? n : -1;
}

/* The number after name= in an #EXT-X-STREAM-INF line, 0 if it isn't there */
static int attribute(const char *line, const char *name)
{
    size_t n = strlen(name);
    const char *p;

    for (p = strstr(line, name); p; p = strstr(p + 1, name))
        if ((p[-1] == ':' || p[-1] == ',') && p[n] == '=')
            return atoi(p + n + 1);
    return 0;
}

/* Fills in the pieces from a playlist, read from pb a line at a time. A
 * list of the same stream in different sizes gets the address of one of
 * those in h->url instead, to be read in its place: returns 1 then. -1 if
 * the stream can't be played. */
static int parse_playlist(hls_t *h, AVIOContext *pb)
{
    char *line = av_malloc(HLS_LINE_MAX);
    int variant = 0;                 /* the line before was #EXT-X-STREAM-INF */
    int height = 0, bandwidth = 0;   /* of that one */
    int best_height = -1, best_bandwidth = 0;
    char *best = NULL;

    if (!line)
        return -1;
    if (next_line(pb, line, HLS_LINE_MAX) < 0 || strncmp(line, "#EXTM3U", 7)) {
        mp_msg(MSGT_STREAM, MSGL_ERR, "[hls] not an HLS playlist\n");
        av_free(line);
        return -1;
    }

    forget_pieces(h);
    h->first = 0;
    h->ended = 0;

    while (next_line(pb, line, HLS_LINE_MAX) >= 0) {
        if (av_strstart(line, "#EXT-X-TARGETDURATION:", NULL)) {
            h->target = atoi(line + 22);
        } else if (av_strstart(line, "#EXT-X-MEDIA-SEQUENCE:", NULL)) {
            h->first = strtoll(line + 22, NULL, 10);
        } else if (av_strstart(line, "#EXT-X-ENDLIST", NULL)) {
            h->ended = 1;
        } else if ((av_strstart(line, "#EXT-X-KEY:", NULL) && !strstr(line, "METHOD=NONE")) ||
                   av_strstart(line, "#EXT-X-MAP:", NULL) ||
                   av_strstart(line, "#EXT-X-BYTERANGE:", NULL)) {
            mp_msg(MSGT_STREAM, MSGL_ERR, "[hls] can't play this stream: %.40s\n", line);
            av_free(best);
            av_free(line);
            return -1;
        } else if (av_strstart(line, "#EXT-X-STREAM-INF:", NULL)) {
            const char *r = strstr(line, "RESOLUTION=");
            const char *x = r ? strchr(r, 'x') : NULL;
            variant = 1;
            height = x ? atoi(x + 1) : 0;
            bandwidth = attribute(line, "BANDWIDTH");
        } else if (line[0] != '#' && line[0]) {
            if (variant) {
                /* the tallest picture up to HLS_TALLEST, else the smallest stream */
                int fits = height && height <= HLS_TALLEST;
                int best_fits = best_height > 0 && best_height <= HLS_TALLEST;
                if (!best || (fits && (!best_fits || height > best_height)) ||
                    (!fits && !best_fits && bandwidth < best_bandwidth)) {
                    av_free(best);
                    best = make_url(h->url, line);
                    best_height = height;
                    best_bandwidth = bandwidth;
                }
                variant = 0;
            } else {
                char *u = make_url(h->url, line);
                if (u)
                    add_piece(h, u);
            }
        }
    }

    av_free(line);
    if (best) {
        mp_msg(MSGT_STREAM, MSGL_V, "[hls] picked the %dp stream\n", best_height);
        av_free(h->url);
        h->url = best;
        return 1;
    }
    if (h->target <= 0)
        h->target = 10;
    return h->pieces ? 0 : -1;
}

/* Reads the playlist, from pb the first time; -1 on failure. */
static int load_playlist(hls_t *h, AVIOContext *pb)
{
    int tries, r = -1;

    h->loaded = av_gettime_relative();
    /* a list of sizes leads to one more playlist, and no further */
    for (tries = 0; tries < 2; tries++, pb = NULL) {
        AVIOContext *own = NULL;

        if (!pb) {
            /* a long list is a small download packed */
            AVDictionary *opts = NULL;
            av_dict_copy(&opts, h->opts, 0);
            av_dict_set(&opts, "headers", "Accept-Encoding: gzip\r\n", 0);
            r = avio_open2(&own, h->url, AVIO_FLAG_READ, &h->icb, &opts);
            av_dict_free(&opts);
            if (r < 0) {
                mp_msg(MSGT_STREAM, MSGL_WARN, "[hls] can't read the playlist again\n");
                return -1;
            }
        }
        r = parse_playlist(h, pb ? pb : own);
        avio_closep(&own);
        if (r != 1)
            break;
    }
    return r == 0 ? 0 : -1;
}

hls_t *hls_open(const char *url, AVIOContext *pb, AVDictionary *opts)
{
    hls_t *h = av_mallocz(sizeof(*h));

    if (!h)
        return NULL;
    h->url = av_strdup(url);
    av_dict_copy(&h->opts, opts, 0);
    h->icb.callback = stopping;
    h->icb.opaque = h;
#ifdef GEKKO
    h->opener = LWP_GetSelf();
#endif
    if (!h->url || load_playlist(h, pb) < 0) {
        hls_close(h);
        return NULL;
    }

    /* a live stream starts near its end, as it's happening */
    h->next = h->first;
    if (!h->ended && h->pieces > HLS_LIVE_START)
        h->next = h->first + h->pieces - HLS_LIVE_START;
    mp_msg(MSGT_STREAM, MSGL_V, "[hls] %d pieces of up to %d s%s\n",
           h->pieces, h->target, h->ended ? "" : ", live");
    return h;
}

int hls_read(hls_t *h, unsigned char *buf, int size)
{
    while (h->failures < HLS_MAX_FAILURES) {
        if (h->pb) {
            int r = avio_read(h->pb, buf, size);
            if (r > 0) {
                h->failures = 0;
                return r;
            }
            if (r < 0 && r != AVERROR_EOF)
                h->failures++;
            avio_closep(&h->pb);
            h->next++;
            continue;
        }
        if (stopping(h))
            return -1;

        if (h->next < h->first) {
            /* the list moved on past pieces not read yet */
            mp_msg(MSGT_STREAM, MSGL_WARN, "[hls] skipped %d pieces\n", (int)(h->first - h->next));
            h->next = h->first;
        }
        if (h->next < h->first + h->pieces) {
            AVDictionary *opts = NULL;
            int r;

            av_dict_copy(&opts, h->opts, 0);
            r = avio_open2(&h->pb, h->piece[h->next - h->first], AVIO_FLAG_READ, &h->icb, &opts);
            av_dict_free(&opts);
            if (r < 0 && h->made && h->next == h->first + h->pieces - 1) {
                /* no such piece: the list says what became of the stream */
                h->held -= strlen(h->piece[--h->pieces]) + 1;
                av_freep(&h->piece[h->pieces]);
                h->made = 0;
                h->relist = 1;
                h->failures++;
            } else if (r < 0) {
                mp_msg(MSGT_STREAM, MSGL_WARN, "[hls] can't read piece %"PRId64"\n", h->next);
                h->failures++;
                h->next++;
            }
            continue;
        }
        if (h->ended)
            return -1;

        /* all of a live stream's pieces are read: ask for the next by
         * its number where the addresses have one */
        if (!h->relist) {
            char *u = next_numbered(h);
            if (u) {
                add_piece(h, u);
                h->made = 1;
                continue;
            }
        }

        /* else read the list again once it's had time to grow, half a
         * piece's length after the last time, as the HLS specification
         * asks */
        if (wait_until(h, (h->relist ? av_gettime_relative() : h->loaded) + h->target * 500000LL) < 0)
            return -1;
        if (load_playlist(h, NULL) < 0)
            h->failures++;
        else
            h->relist = 0;
    }
    mp_msg(MSGT_STREAM, MSGL_ERR, "[hls] the stream stopped coming\n");
    return -1;
}

void hls_close(hls_t *h)
{
    if (!h)
        return;
    avio_closep(&h->pb);
    forget_pieces(h);
    av_free(h->piece);
    av_free(h->url);
    av_dict_free(&h->opts);
    av_free(h);
}
