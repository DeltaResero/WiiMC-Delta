/*
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

#ifndef MPLAYER_STREAM_HLS_H
#define MPLAYER_STREAM_HLS_H

#include "libavformat/avio.h"
#include "libavutil/dict.h"

typedef struct hls hls_t;

/* Whether url, served as mime (NULL if unknown), is an HLS playlist. */
int hls_is_playlist(const char *url, const char *mime);

/* Reads the HLS playlist at url, which pb has open, and gets ready to read
 * the MPEG-TS pieces it lists as one stream. Every piece and playlist is
 * opened with opts. NULL if the playlist can't be played. */
hls_t *hls_open(const char *url, AVIOContext *pb, AVDictionary *opts);

/* Whether the stream is live: its playlist is still growing. */
int hls_is_live(hls_t *h);

/* Up to size bytes of the stream; -1 at its end or on failure. */
int hls_read(hls_t *h, unsigned char *buf, int size);

void hls_close(hls_t *h);

#endif /* MPLAYER_STREAM_HLS_H */
