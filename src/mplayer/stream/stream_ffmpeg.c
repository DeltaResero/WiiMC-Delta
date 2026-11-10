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

#include "config.h"

#include <stdio.h>
#include <unistd.h>

#include "libavformat/avformat.h"
#include "libavformat/avio.h"
#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "mp_msg.h"
#include "stream.h"
#include "m_option.h"
#include "m_struct.h"
#include "av_helpers.h"
#include "libmpdemux/demuxer.h"
#include "stream_hls.h"
#ifdef CONFIG_NETWORKING
#include "network.h"
#endif

char *lavfstreamopts;

static int fill_buffer(stream_t *s, char *buffer, int max_len)
{
    int r = avio_read(s->priv, buffer, max_len);
    return (r <= 0) ? -1 : r;
}

static int hls_fill_buffer(stream_t *s, char *buffer, int max_len)
{
    return hls_read(s->priv, (unsigned char *)buffer, max_len);
}

static void hls_close_f(stream_t *s)
{
    hls_close(s->priv);
}

static int write_buffer(stream_t *s, char *buffer, int len)
{
    AVIOContext *ctx = s->priv;
    avio_write(s->priv, buffer, len);
    avio_flush(s->priv);
    if (ctx->error)
        return -1;
    return len;
}

static int seek(stream_t *s, int64_t newpos)
{
    s->pos = newpos;
    if (avio_seek(s->priv, s->pos, SEEK_SET) < 0) {
        s->eof = 1;
        return 0;
    }
    return 1;
}

static int control(stream_t *s, int cmd, void *arg)
{
    AVIOContext *ctx = s->priv;
    int64_t size, ts;
    double pts;
    switch(cmd) {
    case STREAM_CTRL_GET_SIZE:
        size = avio_size(s->priv);
        if(size >= 0) {
            *(uint64_t *)arg = size;
            return 1;
        }
        break;
    case STREAM_CTRL_SEEK_TO_TIME:
        pts = *(double *)arg;
        ts = pts * AV_TIME_BASE;
        if (!ctx->read_seek)
            break;
        ts = ctx->read_seek(s->priv, -1, ts, 0);
        if (ts >= 0)
            return 1;
        break;
    }
    return STREAM_UNSUPPORTED;
}

static void close_f(stream_t *stream)
{
    avio_close(stream->priv);
}

static const char prefix[] = "ffmpeg://";

#ifdef GEKKO
const char *YouTubeStreamAgent(void); /* youtube.cpp */

static int googlevideo_itag18(const char *url)
{
    const char *p = url;
    while ((p = strstr(p, "itag=")) != NULL) {
        if (p > url && (p[-1] == '?' || p[-1] == '&') && p[5] == '1' && p[6] == '8' && (p[7] == '&' || p[7] == 0))
            return 1;
        p += 5;
    }
    return 0;
}
#endif

static int open_f(stream_t *stream, int mode, void *opts, int *file_format)
{
    int flags = 0;
    const char *filename;
    AVDictionary *avopts = NULL;
    AVIOContext *ctx = NULL;
    int res = STREAM_ERROR;
    int64_t size;
    int dummy;
    AVDictionary *hlsopts = NULL;
    int is_hls = 0;

    init_avformat();
    avformat_network_init();
    if (mode == STREAM_READ)
        flags = AVIO_FLAG_READ;
    else if (mode == STREAM_WRITE)
        flags = AVIO_FLAG_WRITE;
    else {
        mp_msg(MSGT_OPEN, MSGL_ERR, "[ffmpeg] Unknown open mode %d\n", mode);
        res = STREAM_UNSUPPORTED;
        goto out;
    }

#ifdef AVIO_FLAG_DIRECT
    flags |= AVIO_FLAG_DIRECT;
#else
    mp_msg(MSGT_OPEN, MSGL_WARN, "[ffmpeg] No support for AVIO_FLAG_DIRECT, might cause performance and other issues.\n"
                                 "Please update to and rebuild against an FFmpeg version supporting it.\n");
#endif

    if (stream->url)
        filename = stream->url;
    else {
        mp_msg(MSGT_OPEN, MSGL_ERR, "[ffmpeg] No URL\n");
        goto out;
    }
    if (!strncmp(filename, prefix, strlen(prefix)))
        filename += strlen(prefix);
    dummy = !strncmp(filename, "rtsp:", 5) || !strncmp(filename, "dummy:", 6);
    mp_msg(MSGT_OPEN, MSGL_V, "[ffmpeg] Opening %s\n", filename);

    if (lavfstreamopts && av_dict_parse_string(&avopts, lavfstreamopts, "=", ",", 0) < 0) {
        mp_msg(MSGT_HEADER,MSGL_ERR, "Your options /%s/ look like gibberish to me pal\n", lavfstreamopts);
        goto out;
    }

#ifdef GEKKO
    /* FFmpeg accepts any server certificate unless told otherwise. WiiMC
     * always verifies https servers, against the CA bundle in its folder;
     * without that file no https connection can succeed. */
    if (!strncmp(filename, "https:", 6)) {
        extern char appPath[];
        extern int socInit(void);
        char ca_file[1024];
        /* FFmpeg's tcp uses libogc's BSD sockets, whose "soc:" device
         * only socInit() registers. WiiMC's network thread calls it once
         * the network is up, but a URL given on the command line can be
         * opened before that; socInit() does nothing if it already ran. */
        socInit();
        snprintf(ca_file, sizeof(ca_file), "%s/cacert.pem", appPath);
        if (access(ca_file, R_OK) != 0)
            mp_msg(MSGT_OPEN, MSGL_ERR, "[ffmpeg] https needs a CA bundle at %s to verify servers\n", ca_file);
        av_dict_set(&avopts, "tls_verify", "1", 0);
        av_dict_set(&avopts, "ca_file", ca_file, 0);

        /* YouTube slows a separate video or audio stream that's read in one
         * open-ended request to below its bitrate after the first 256 KB,
         * but not one read in pieces: ask for 1 MB at a time, on one kept
         * connection. itag 18, with sound and picture in one file, isn't
         * slowed, and a live stream's playlists aren't files. */
        if (strstr(filename, ".googlevideo.com/") && !googlevideo_itag18(filename) &&
            !strstr(filename, "/api/manifest/")) {
            av_dict_set(&avopts, "request_size", "1048576", 0);
            av_dict_set(&avopts, "multiple_requests", "1", 0);
        }

        /* YouTube cuts off a stream that's read slowly, as a separate audio
         * stream is: it has no cache, so it's read as it plays. Pick it up
         * again where it stopped instead of ending the sound. */
        if (strstr(filename, ".googlevideo.com/"))
            av_dict_set(&avopts, "reconnect", "1", 0);

        /* fetch a YouTube video as the client that was given its links */
        if (strstr(filename, ".googlevideo.com/") && YouTubeStreamAgent())
            av_dict_set(&avopts, "user_agent", YouTubeStreamAgent(), 0);
    }
#endif

    /* an HLS stream's pieces are opened as its playlist is */
    av_dict_copy(&hlsopts, avopts, 0);

    /* a playlist with hours of pieces is megabytes long, but packs small */
    if (!strncmp(filename, "http", 4) && hls_is_playlist(filename, NULL))
        av_dict_set(&avopts, "headers", "Accept-Encoding: gzip\r\n", 0);

    if (!dummy && avio_open2(&ctx, filename, flags, NULL, &avopts) < 0)
        goto out;

    if (!dummy && av_dict_count(avopts)) {
        AVDictionaryEntry *e = NULL;
        while ((e = av_dict_get(avopts, "", e, AV_DICT_IGNORE_SUFFIX))) {
            mp_msg(MSGT_HEADER,MSGL_ERR,"Unknown option %s\n", e->key);
        }
        goto out;
    }
    av_dict_free(&avopts);

    stream->priv = ctx;
    size = dummy ? 0 : avio_size(ctx);
    if (size >= 0)
        stream->end_pos = size;
    stream->type = STREAMTYPE_FILE;
    stream->seek = seek;
    if (dummy || !ctx->seekable) {
        stream->type = STREAMTYPE_STREAM;
        stream->seek = NULL;
    }
#ifdef CONFIG_NETWORKING
    /* Pick the demuxer from the server's Content-Type, as MPlayer's own
     * http stream does. Without it a live stream is probed, which reads
     * 1.5 MB before playback starts (over a minute at radio bitrates). */
    if (!dummy && !strncmp(filename, "http", 4)) {
        uint8_t *mime = NULL;
        if (av_opt_get(ctx, "mime_type", AV_OPT_SEARCH_CHILDREN, &mime) >= 0 && mime) {
            char *semi = strchr((char *)mime, ';');
            int i;
            if (semi)
                *semi = '\0';
            mp_msg(MSGT_OPEN, MSGL_V, "[ffmpeg] Content-Type: %s\n", (char *)mime);
            is_hls = hls_is_playlist(filename, (char *)mime);
            for (i = 0; mime_type_table[i].mime_type; i++)
                if (!av_strcasecmp((char *)mime, mime_type_table[i].mime_type)) {
                    *file_format = mime_type_table[i].demuxer_type;
                    break;
                }
        }
        av_free(mime);
        is_hls = is_hls || hls_is_playlist(filename, NULL);
    }

    /* An HLS playlist plays as the MPEG-TS pieces it lists, one after
     * another, through MPlayer's cache, which fetches them ahead. */
    if (is_hls) {
        hls_t *hls = hls_open(filename, ctx, hlsopts);
        avio_closep(&ctx);
        stream->priv = hls;
        if (!hls)
            goto out;
        stream->end_pos = 0;
        stream->type = STREAMTYPE_STREAM;
        stream->seek = NULL;
        if (hls_is_live(hls))
            stream->flags |= MP_STREAM_LIVE;
        stream->fill_buffer = hls_fill_buffer;
        stream->close = hls_close_f;
        res = STREAM_OK;
        goto out;
    }
#endif
    if (dummy) {
        *file_format = DEMUXER_TYPE_LAVF;
    } else {
        stream->fill_buffer = fill_buffer;
        stream->write_buffer = write_buffer;
        stream->control = control;
        stream->close = close_f;
    }
    res = STREAM_OK;

out:
    av_dict_free(&hlsopts);
    return res;
}

const stream_info_t stream_info_ffmpeg = {
  "FFmpeg",
  "ffmpeg",
  "",
  "",
  open_f,
  { "ffmpeg", "rtmp", "rtsp", "http", "https", "gopher", NULL },
  NULL,
  1 // Urls are an option string
};
