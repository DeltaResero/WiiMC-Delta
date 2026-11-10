/****************************************************************************
 * WiiMC
 *
 * ytparse.cpp
 *
 * Reads what WiiMC needs from YouTube's replies: videos and playlists from
 * search and browse pages, the token for the next page, search suggestions,
 * and the playable link from a player reply. It works on the reply text in
 * place without building a tree, so a reply of a few MB costs no memory
 * beyond itself, and a reply cut short just ends the list early.
 *
 * Which fields hold what follows BrewTube's youtube.cpp (ReviveMii, GPL-3.0).
 ***************************************************************************/

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "ytparse.h"

static const char *ws(const char *p, const char *e)
{
    while (p < e && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t'))
        p++;
    return p;
}

// the end of the string whose opening quote is at p, or NULL when cut short
static const char *str_end(const char *p, const char *e)
{
    for (p++; p < e; p++)
    {
        if (*p == '\\')
            p++;
        else if (*p == '"')
            return p + 1;
    }
    return NULL;
}

// the end of the value at p, or NULL when cut short
static const char *skip(const char *p, const char *e)
{
    p = ws(p, e);
    if (p >= e)
        return NULL;

    if (*p == '"')
        return str_end(p, e);

    if (*p == '{' || *p == '[')
    {
        int depth = 0;
        while (p < e)
        {
            char c = *p;
            if (c == '"')
            {
                p = str_end(p, e);
                if (!p)
                    return NULL;
                continue;
            }
            if (c == '{' || c == '[')
                depth++;
            else if ((c == '}' || c == ']') && --depth == 0)
                return p + 1;
            p++;
        }
        return NULL;
    }

    // a number, true, false, or null
    while (p < e && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\n' && *p != '\r' && *p != '\t')
        p++;
    return p < e ? p : NULL;
}

// the value of key in the object at p, or NULL
static const char *member(const char *p, const char *e, const char *key)
{
    if (!p)
        return NULL;
    p = ws(p, e);
    if (p >= e || *p != '{')
        return NULL;

    size_t klen = strlen(key);
    p++;

    while (1)
    {
        p = ws(p, e);
        if (p >= e || *p != '"')
            return NULL;

        const char *k = p + 1;
        const char *ke = str_end(p, e);
        if (!ke)
            return NULL;

        p = ws(ke, e);
        if (p >= e || *p != ':')
            return NULL;
        p = ws(p + 1, e);

        if ((size_t)(ke - 1 - k) == klen && memcmp(k, key, klen) == 0)
            return p;

        p = skip(p, e);
        if (!p)
            return NULL;
        p = ws(p, e);
        if (p >= e || *p != ',')
            return NULL;
        p++;
    }
}

// element i of the array at p, or NULL
static const char *element(const char *p, const char *e, int i)
{
    if (!p)
        return NULL;
    p = ws(p, e);
    if (p >= e || *p != '[')
        return NULL;
    p = ws(p + 1, e);
    if (p < e && *p == ']')
        return NULL;

    for (; i > 0; i--)
    {
        p = skip(p, e);
        if (!p)
            return NULL;
        p = ws(p, e);
        if (p >= e || *p != ',')
            return NULL;
        p = ws(p + 1, e);
    }
    return p < e ? p : NULL;
}

// follows a dotted path such as "title.runs.0.text"; numbers index arrays
static const char *path(const char *p, const char *e, const char *route)
{
    char seg[64];

    while (p && *route)
    {
        size_t n = strcspn(route, ".");
        if (n >= sizeof(seg))
            return NULL;
        memcpy(seg, route, n);
        seg[n] = 0;
        route += n + (route[n] == '.');

        if (seg[0] >= '0' && seg[0] <= '9')
            p = element(p, e, atoi(seg));
        else
            p = member(p, e, seg);
    }
    return p;
}

static void put_utf8(char *out, size_t *n, unsigned cp)
{
    if (cp < 0x80)
        out[(*n)++] = cp;
    else if (cp < 0x800)
    {
        out[(*n)++] = 0xC0 | (cp >> 6);
        out[(*n)++] = 0x80 | (cp & 0x3F);
    }
    else if (cp < 0x10000)
    {
        out[(*n)++] = 0xE0 | (cp >> 12);
        out[(*n)++] = 0x80 | ((cp >> 6) & 0x3F);
        out[(*n)++] = 0x80 | (cp & 0x3F);
    }
    else
    {
        out[(*n)++] = 0xF0 | (cp >> 18);
        out[(*n)++] = 0x80 | ((cp >> 12) & 0x3F);
        out[(*n)++] = 0x80 | ((cp >> 6) & 0x3F);
        out[(*n)++] = 0x80 | (cp & 0x3F);
    }
}

static unsigned hex4(const char *p, const char *e)
{
    if (e - p < 4)
        return 0;
    char h[5] = { p[0], p[1], p[2], p[3], 0 };
    return strtoul(h, NULL, 16);
}

// the string at p, unescaped into UTF-8; a long string is cut at a whole character
static bool getstr(const char *p, const char *e, char *out, size_t size)
{
    out[0] = 0;
    if (!p || size < 5)
        return false;
    p = ws(p, e);
    if (p >= e || *p != '"')
        return false;

    const char *end = str_end(p, e);
    if (!end)
        return false;
    end--; // the closing quote
    p++;

    size_t n = 0;
    while (p < end && n + 4 < size)
    {
        char c = *p++;
        if (c != '\\')
        {
            out[n++] = c;
            continue;
        }
        if (p >= end)
            break;
        c = *p++;
        switch (c)
        {
            case 'n': out[n++] = '\n'; break;
            case 't': out[n++] = '\t'; break;
            case 'r': out[n++] = '\r'; break;
            case 'b': case 'f': break;
            case 'u':
            {
                unsigned cp = hex4(p, end);
                p += 4;
                // a surrogate pair is one character
                if (cp >= 0xD800 && cp < 0xDC00 && end - p >= 6 && p[0] == '\\' && p[1] == 'u')
                {
                    unsigned lo = hex4(p + 2, end);
                    if (lo >= 0xDC00 && lo < 0xE000)
                    {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        p += 6;
                    }
                }
                if (cp >= 0xD800 && cp < 0xE000)
                    cp = 0xFFFD;
                put_utf8(out, &n, cp);
                break;
            }
            default: out[n++] = c; break; // \" \\ \/
        }
    }

    // stopped for room: drop a character left half copied
    if (p < end && n > 0)
    {
        size_t k = n;
        while (k > 0 && (out[k - 1] & 0xC0) == 0x80)
            k--;
        if (k > 0 && (out[k - 1] & 0x80))
        {
            unsigned char lead = out[k - 1];
            size_t want = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : 2;
            if (n - (k - 1) < want)
                n = k - 1;
        }
    }
    out[n] = 0;
    return true;
}

// Drops what WiiMC's font can't draw, which would show as boxes: emoji,
// the joiners and variation selectors that build them, and the keycap mark.
// Then collapses the spaces left behind.
static void tidy(char *s)
{
    unsigned char *r = (unsigned char *)s, *w = (unsigned char *)s;

    while (*r)
    {
        unsigned cp = *r;
        int n = 1;
        if (cp >= 0xF0 && r[1] && r[2] && r[3])
        {
            cp = ((cp & 0x07) << 18) | ((r[1] & 0x3F) << 12) | ((r[2] & 0x3F) << 6) | (r[3] & 0x3F);
            n = 4;
        }
        else if (cp >= 0xE0 && r[1] && r[2])
        {
            cp = ((cp & 0x0F) << 12) | ((r[1] & 0x3F) << 6) | (r[2] & 0x3F);
            n = 3;
        }

        bool drop = cp >= 0x1F000 || (cp >= 0xFE00 && cp <= 0xFE0F) || cp == 0x200D || cp == 0x20E3 ||
            (cp >= 0x2600 && cp <= 0x27BF) || (cp >= 0x2B00 && cp <= 0x2BFF);

        if (drop || (*r == ' ' && (w == (unsigned char *)s || w[-1] == ' ')))
            r += drop ? n : 1;
        else
        {
            memmove(w, r, n);
            w += n;
            r += n;
        }
    }
    while (w > (unsigned char *)s && w[-1] == ' ')
        w--;
    *w = 0;
}

// YouTube's text objects: {"simpleText":..}, {"runs":[{"text":..},..]}, or {"content":..}
static void text(const char *p, const char *e, char *out, size_t size)
{
    out[0] = 0;
    if (!p)
        return;

    const char *v;
    if ((v = member(p, e, "simpleText")) || (v = member(p, e, "content")))
    {
        getstr(v, e, out, size);
        return;
    }

    v = member(p, e, "runs");
    size_t n = 0;
    for (int i = 0; v && n + 1 < size; i++)
    {
        const char *run = element(v, e, i);
        if (!run)
            break;
        getstr(member(run, e, "text"), e, out + n, size - n);
        n += strlen(out + n);
    }
}

static void first_text(const char *obj, const char *e, const char *const *keys, char *out, size_t size)
{
    out[0] = 0;
    for (int i = 0; keys[i] && !out[0]; i++)
        text(member(obj, e, keys[i]), e, out, size);
}

// videoRenderer, gridVideoRenderer, compactVideoRenderer, playlistVideoRenderer, and
// videoCardRenderer (the Gaming channel page)
static bool video_item(const char *vr, const char *e, YtItem *it)
{
    if (!getstr(member(vr, e, "videoId"), e, it->id, sizeof(it->id)) || !it->id[0])
        return false;

    text(member(vr, e, "title"), e, it->title, sizeof(it->title));

    static const char *const channel[] = { "ownerText", "longBylineText", "shortBylineText", "bylineText", NULL };
    first_text(vr, e, channel, it->channel, sizeof(it->channel));

    text(member(vr, e, "lengthText"), e, it->length, sizeof(it->length));
    for (int i = 0; !it->length[0]; i++)
    {
        const char *ov = element(member(vr, e, "thumbnailOverlays"), e, i);
        if (!ov)
            break;
        text(path(ov, e, "thumbnailOverlayTimeStatusRenderer.text"), e, it->length, sizeof(it->length));
    }

    static const char *const views[] = { "viewCountText", "shortViewCountText", "videoInfo", "metadataText", NULL };
    first_text(vr, e, views, it->views, sizeof(it->views));
    text(member(vr, e, "publishedTimeText"), e, it->age, sizeof(it->age));

    // playlist pages put "views . age" together in videoInfo
    if (!strcmp(it->views, it->age))
        it->age[0] = 0;
    tidy(it->title);
    tidy(it->channel);
    return true;
}

// lockupViewModel: playlists and mixes in search, and most items on topic pages
static bool lockup_item(const char *lvm, const char *e, YtItem *it)
{
    char type[48];
    getstr(member(lvm, e, "contentType"), e, type, sizeof(type));

    if (!getstr(member(lvm, e, "contentId"), e, it->id, sizeof(it->id)) || !it->id[0])
        return false;

    it->playlist = strcmp(type, "LOCKUP_CONTENT_TYPE_VIDEO") != 0;

    const char *meta = path(lvm, e, "metadata.lockupMetadataViewModel");
    text(member(meta, e, "title"), e, it->title, sizeof(it->title));
    tidy(it->title);
    if (!it->title[0])
        return false;

    const char *rows = path(meta, e, "metadata.contentMetadataViewModel.metadataRows");
    text(path(rows, e, "0.metadataParts.0.text"), e, it->channel, sizeof(it->channel));
    tidy(it->channel);
    text(path(rows, e, "1.metadataParts.0.text"), e, it->views, sizeof(it->views));
    text(path(rows, e, "1.metadataParts.1.text"), e, it->age, sizeof(it->age));

    const char *image = member(lvm, e, "contentImage");
    const char *tvm = member(image, e, "thumbnailViewModel");
    if (!tvm)
        tvm = path(image, e, "collectionThumbnailViewModel.primaryThumbnail.thumbnailViewModel");

    // a playlist's picture is one of its videos': .../vi/VIDEOID/hq720.jpg,
    // or its own square art, as podcasts and YouTube Music's playlists have
    // (i.ytimg.com, i9.ytimg.com and the like)
    char url[256];
    if (it->playlist && getstr(path(tvm, e, "image.sources.0.url"), e, url, sizeof(url)))
    {
        const char *v = strstr(url, "/vi/");
        const char *host = url + 8; // after "https://"
        size_t hn = strcspn(host, "/");
        if (!v && strncmp(url, "https://i", 9) == 0 && hn > 10 &&
            strncmp(host + hn - 10, ".ytimg.com", 10) == 0)
            snprintf(it->image, sizeof(it->image), "%s", url);
        if (v)
        {
            v += 4;
            size_t n = strcspn(v, "/?");
            if (n > 0 && n < sizeof(it->thumb))
            {
                memcpy(it->thumb, v, n);
                it->thumb[n] = 0;
            }
        }
    }

    for (int i = 0; tvm && !it->length[0]; i++)
    {
        const char *ov = element(member(tvm, e, "overlays"), e, i);
        if (!ov)
            break;
        getstr(path(ov, e, "thumbnailOverlayBadgeViewModel.thumbnailBadges.0.thumbnailBadgeViewModel.text"),
            e, it->length, sizeof(it->length));
        if (!it->length[0])
            getstr(path(ov, e, "thumbnailBottomOverlayViewModel.badges.0.thumbnailBadgeViewModel.text"),
                e, it->length, sizeof(it->length));
    }
    return true;
}

#define MAX_KEYS 8

// where each key next appears as a key, so each is searched for once per pass
typedef struct
{
    const char *const *keys;
    const char *at[MAX_KEYS]; // next place found, or NULL once there are no more
    bool done[MAX_KEYS];
} key_scan;

// the value of the next key at or after p, for any of the keys; which = its index
static const char *next_key(key_scan *ks, const char *p, const char *e, int *which)
{
    const char *best = NULL;

    for (int i = 0; ks->keys[i]; i++)
    {
        if (ks->done[i])
            continue;

        if (!ks->at[i] || ks->at[i] < p)
        {
            size_t n = strlen(ks->keys[i]);
            const char *q = p;
            ks->at[i] = NULL;

            while (q < e && (q = (const char *)memmem(q, e - q, ks->keys[i], n)))
            {
                const char *v = ws(q + n, e);
                if (v < e && *v == ':')
                {
                    ks->at[i] = q;
                    break;
                }
                q += n;
            }
            if (!ks->at[i])
            {
                ks->done[i] = true;
                continue;
            }
        }

        if (!best || ks->at[i] < best)
        {
            best = ks->at[i];
            *which = i;
        }
    }
    return best ? ws(ws(best + strlen(ks->keys[*which]), e) + 1, e) : NULL;
}

int YtParseItems(const char *json, size_t len, YtItem *items, int max, char *next, size_t nextsize)
{
    static const char *const keys[] = {
        "\"videoRenderer\"", "\"gridVideoRenderer\"", "\"compactVideoRenderer\"",
        "\"playlistVideoRenderer\"", "\"videoCardRenderer\"", "\"lockupViewModel\"",
        "\"continuationItemRenderer\"", NULL
    };
    const char *p = json, *e = json + len;
    int count = 0, which = 0;
    key_scan ks;

    memset(&ks, 0, sizeof(ks));
    ks.keys = keys;

    if (next && nextsize)
        next[0] = 0;

    while (count < max && (p = next_key(&ks, p, e, &which)))
    {
        const char *end = skip(p, e);
        if (!end)
            break; // cut short

        if (keys[which + 1] == NULL) // continuationItemRenderer
        {
            if (next && nextsize && !next[0])
                getstr(path(p, e, "continuationEndpoint.continuationCommand.token"), e, next, nextsize);
            p = end;
            continue;
        }

        YtItem *it = &items[count];
        memset(it, 0, sizeof(*it));
        bool ok = strcmp(keys[which], "\"lockupViewModel\"") == 0 ? lockup_item(p, e, it) : video_item(p, e, it);

        for (int i = 0; ok && i < count; i++)
            if (!strcmp(items[i].id, it->id))
                ok = false;

        if (ok)
            count++;
        p = end;
    }
    return count;
}

int YtParseSuggestions(const char *reply, size_t len, char (*out)[96], int max)
{
    const char *e = reply + len;
    const char *p = (const char *)memchr(reply, '[', len);
    const char *list = element(p, e, 1);
    int n = 0;

    for (int i = 0; n < max; i++)
    {
        const char *s = element(list, e, i);
        if (!s)
            break;
        if (getstr(element(s, e, 0), e, out[n], sizeof(out[n])) && out[n][0])
            n++;
    }
    return n;
}

bool YtParseVisitor(const char *reply, size_t len, char *out, size_t size)
{
    // the reply starts with )]}' on a line of its own
    const char *e = reply + len;
    const char *p = (const char *)memchr(reply, '\n', len);
    return p && getstr(path(p + 1, e, "0.2.0.0.13"), e, out, size) && out[0];
}

bool YtParsePlayer(const char *json, size_t len, char *url, size_t urlsize, char *reason, size_t reasonsize)
{
    const char *e = json + len;
    char status[32];

    url[0] = 0;
    if (reason && reasonsize)
        reason[0] = 0;

    const char *ps = member(json, e, "playabilityStatus");
    getstr(member(ps, e, "status"), e, status, sizeof(status));
    if (strcmp(status, "OK") != 0)
    {
        if (reason && reasonsize)
            getstr(member(ps, e, "reason"), e, reason, reasonsize);
        return false;
    }

    const char *formats = path(json, e, "streamingData.formats");
    for (int i = 0; ; i++)
    {
        const char *f = element(formats, e, i);
        if (!f)
            break;

        const char *itag = member(f, e, "itag");
        if (itag && atoi(itag) == 18)
            return getstr(member(f, e, "url"), e, url, urlsize) && url[0];
    }
    return false;
}

int YtParseCaptions(const char *json, size_t len, YtCaption *out, int max)
{
    const char *e = json + len;
    const char *tracks = path(json, e, "captions.playerCaptionsTracklistRenderer.captionTracks");
    int n = 0;
    char kind[16];

    // two passes: the uploader's tracks, then the automatic (kind "asr") ones
    for (int pass = 0; pass < 2 && tracks; pass++)
    {
        for (int i = 0; n < max; i++)
        {
            const char *t = element(tracks, e, i);
            if (!t)
                break;
            getstr(member(t, e, "kind"), e, kind, sizeof(kind));
            if ((strcmp(kind, "asr") == 0) != (pass == 1))
                continue;
            if (!getstr(member(t, e, "baseUrl"), e, out[n].url, sizeof(out[n].url)) ||
                strncmp(out[n].url, "https://", 8) != 0)
                continue;
            text(member(t, e, "name"), e, out[n].name, sizeof(out[n].name));
            if (!out[n].name[0])
                getstr(member(t, e, "languageCode"), e, out[n].name, sizeof(out[n].name));
            n++;
        }
    }
    return n;
}

int YtParseAdaptive(const char *json, size_t len, int maxHeight, char *vurl, size_t vsize, int *height,
    YtAudioTrack *audio, int maxAudio)
{
    const char *e = json + len;
    const char *formats = path(json, e, "streamingData.adaptiveFormats");
    const char *bestv = NULL;
    int bh = 0, bb = 0, n = 0;
    char mime[64];

    vurl[0] = 0;
    *height = 0;

    for (int i = 0; ; i++)
    {
        const char *f = element(formats, e, i);
        if (!f)
            break;

        getstr(member(f, e, "mimeType"), e, mime, sizeof(mime));
        const char *it = member(f, e, "itag");
        const char *h = member(f, e, "height");
        const char *b = member(f, e, "bitrate");
        int itag = it ? atoi(it) : 0, hh = h ? atoi(h) : 0, bt = b ? atoi(b) : 0;

        if (strstr(mime, "avc1") && hh > 0 && hh <= maxHeight && (hh > bh || (hh == bh && bt > bb)))
        {
            bestv = f;
            bh = hh;
            bb = bt;
        }
        else if (itag == 140 && n < maxAudio)
        {
            // a copy with its loudness evened out, which comes beside the original
            const char *drc = member(f, e, "isDrc");
            if (drc && strncmp(drc, "true", 4) == 0)
                continue;

            YtAudioTrack *t = &audio[n];
            const char *at = member(f, e, "audioTrack");
            char isdef[8] = "";
            getstr(member(at, e, "displayName"), e, t->name, sizeof(t->name));
            getstr(member(f, e, "url"), e, t->url, sizeof(t->url));
            const char *d = member(at, e, "audioIsDefault");
            if (d)
                snprintf(isdef, sizeof(isdef), "%.4s", d);
            t->original = !at || strncmp(isdef, "true", 4) == 0 || strstr(t->name, "original");
            n++;
        }
    }

    // the original first, the others in the reply's order
    for (int i = 1; i < n; i++)
    {
        if (!audio[i].original || audio[0].original)
            continue;
        YtAudioTrack t = audio[i];
        memmove(&audio[1], &audio[0], i * sizeof(YtAudioTrack));
        audio[0] = t;
        break;
    }

    if (bestv && getstr(member(bestv, e, "url"), e, vurl, vsize) && vurl[0])
        *height = bh;
    else
        vurl[0] = 0;
    return n;
}

// an attribute's number in a tag that runs from p to e, or -1 when it's
// missing or too big to be a time in milliseconds
static long xml_num(const char *p, const char *e, const char *name)
{
    size_t n = strlen(name);
    for (; p + n + 2 < e; p++)
    {
        if (p[-1] != ' ' || memcmp(p, name, n) != 0 || p[n] != '=' || p[n + 1] != '"')
            continue;

        const char *q = p + n + 2;
        long v = 0;
        if (q >= e || *q < '0' || *q > '9')
            return -1;
        for (; q < e && *q >= '0' && *q <= '9'; q++)
        {
            if (v > 99999999) // past 27 hours
                return -1;
            v = v * 10 + (*q - '0');
        }
        return v;
    }
    return -1;
}

// the text between p and e without tags, with entities decoded; returns length
static size_t xml_text(const char *p, const char *e, char *out, size_t size)
{
    size_t n = 0;

    while (p < e && n + 5 < size)
    {
        if (*p == '<')
        {
            const char *gt = (const char *)memchr(p, '>', e - p);
            if (!gt)
                break;
            if (e - p >= 4 && strncmp(p, "<br", 3) == 0)
                out[n++] = '\n';
            p = gt + 1;
            continue;
        }
        if (*p == '&')
        {
            const char *semi = (const char *)memchr(p, ';', e - p < 10 ? e - p : 10);
            if (semi)
            {
                unsigned cp = 0;
                if (p[1] == '#')
                    cp = p[2] == 'x' ? strtoul(p + 3, NULL, 16) : strtoul(p + 2, NULL, 10);
                else if (strncmp(p, "&amp;", 5) == 0) cp = '&';
                else if (strncmp(p, "&lt;", 4) == 0) cp = '<';
                else if (strncmp(p, "&gt;", 4) == 0) cp = '>';
                else if (strncmp(p, "&quot;", 6) == 0) cp = '"';
                else if (strncmp(p, "&apos;", 6) == 0) cp = '\'';
                if (cp > 0 && cp < 0x110000 && !(cp >= 0xD800 && cp < 0xE000))
                {
                    put_utf8(out, &n, cp);
                    p = semi + 1;
                    continue;
                }
            }
        }
        if (*p != '\r')
            out[n++] = *p;
        p++;
    }
    out[n] = 0;

    // trim spaces and line breaks at both ends
    size_t a = 0;
    while (out[a] == ' ' || out[a] == '\n')
        a++;
    while (n > a && (out[n - 1] == ' ' || out[n - 1] == '\n'))
        n--;
    memmove(out, out + a, n - a);
    n -= a;
    out[n] = 0;
    return n;
}

static void srt_time(char *buf, size_t size, long ms)
{
    snprintf(buf, size, "%02ld:%02ld:%02ld,%03ld", ms / 3600000, ms / 60000 % 60, ms / 1000 % 60, ms % 1000);
}

size_t YtCaptionsToSrt(const char *xml, size_t len, char *out, size_t size)
{
    const char *e = xml + len;
    const char *p = xml;
    size_t n = 0;
    int count = 0;
    char line[1024];

    out[0] = 0;
    while (p < e)
    {
        // the next <p ...>
        const char *tag = NULL;
        for (; p + 3 < e; p++)
            if (p[0] == '<' && p[1] == 'p' && (p[2] == ' ' || p[2] == '>'))
            {
                tag = p;
                break;
            }
        if (!tag)
            break;
        const char *gt = (const char *)memchr(tag, '>', e - tag);
        if (!gt)
            break;
        long t = xml_num(tag + 2, gt, "t");
        long d = xml_num(tag + 2, gt, "d");

        const char *body = gt + 1;
        const char *close = body;
        while (close + 4 <= e && strncmp(close, "</p>", 4) != 0)
            close++;
        if (close + 4 > e)
            break;
        p = close + 4;

        if (t < 0 || d <= 0 || gt[-1] == '/')
            continue;
        if (!xml_text(body, close, line, sizeof(line)))
            continue;

        // end where the next timed line starts, if that's sooner
        long end = t + d;
        const char *q = p;
        for (; q + 3 < e; q++)
            if (q[0] == '<' && q[1] == 'p' && q[2] == ' ')
            {
                const char *qgt = (const char *)memchr(q, '>', e - q);
                long nt = qgt ? xml_num(q + 2, qgt, "t") : -1;
                if (nt > t && nt < end)
                    end = nt;
                break;
            }

        char a[24], b[24];
        srt_time(a, sizeof(a), t);
        srt_time(b, sizeof(b), end);
        int k = snprintf(out + n, size - n, "%d\n%s --> %s\n%s\n\n", ++count, a, b, line);
        if (k < 0 || (size_t)k >= size - n)
        {
            out[n] = 0;
            break;
        }
        n += k;
    }
    return n;
}

// RiiTube's search comes as Navi-X lines: type=, name=, URL=, thumb=
static bool starts(const char *p, const char *le, const char *s)
{
    size_t n = strlen(s);
    return (size_t)(le - p) >= n && memcmp(p, s, n) == 0;
}

static void copy_line(const char *p, const char *le, char *out, size_t size)
{
    size_t n = le - p;
    if (n >= size)
        n = size - 1;
    memcpy(out, p, n);
    out[n] = 0;
}

// the video ID in a RiiTube video link, .../video/wii/?q=ID
static bool riitube_id(const char *p, const char *le, char *id, size_t size)
{
    static const char key[] = "/video/wii/?q=";
    size_t k = sizeof(key) - 1;

    for (; p + k <= le; p++)
    {
        if (memcmp(p, key, k) != 0)
            continue;
        p += k;
        size_t n = 0;
        while (p + n < le && p[n] != '&')
        {
            char c = p[n];
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
                return false;
            n++;
        }
        if (n == 0 || n >= size)
            return false;
        memcpy(id, p, n);
        id[n] = 0;
        return true;
    }
    return false;
}

int YtParseRiiTube(const char *text, size_t len, YtItem *items, int max, char *next, size_t nextsize)
{
    const char *p = text, *e = text + len;
    char name[256] = "";
    bool nextPage = false;
    int count = 0;

    if (next && nextsize)
        next[0] = 0;

    while (p < e)
    {
        const char *le = (const char *)memchr(p, '\n', e - p);
        const char *after = le ? le + 1 : e;
        if (!le)
            le = e;
        if (le > p && le[-1] == '\r')
            le--;

        if (starts(p, le, "name="))
        {
            copy_line(p + 5, le, name, sizeof(name));
            nextPage = strncmp(name, "Next page", 9) == 0;
        }
        else if (starts(p, le, "URL="))
        {
            const char *u = p + 4;
            if (nextPage)
            {
                if (next && nextsize)
                    copy_line(u, le, next, nextsize);
            }
            else if (name[0] && count < max)
            {
                YtItem *it = &items[count];
                memset(it, 0, sizeof(*it));
                if (riitube_id(u, le, it->id, sizeof(it->id)))
                {
                    snprintf(it->title, sizeof(it->title), "%s", name);
                    tidy(it->title);
                    count++;
                }
            }
            name[0] = 0;
            nextPage = false;
        }
        p = after;
    }
    return count;
}

void YtSearchParams(int sort, int date, int type, int duration, char *out, size_t size)
{
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    unsigned char msg[16], filter[8];
    int n = 0, f = 0;

    // field 2: the filters, each a one-byte varint
    if (date > 0 && date < 128) { filter[f++] = 0x08; filter[f++] = date; }
    if (type > 0 && type < 128) { filter[f++] = 0x10; filter[f++] = type; }
    if (duration > 0 && duration < 128) { filter[f++] = 0x18; filter[f++] = duration; }

    // field 1: the order
    if (sort > 0 && sort < 128) { msg[n++] = 0x08; msg[n++] = sort; }
    if (f)
    {
        msg[n++] = 0x12;
        msg[n++] = f;
        memcpy(msg + n, filter, f);
        n += f;
    }

    size_t o = 0;
    for (int i = 0; i < n && o + 4 < size; i += 3)
    {
        unsigned v = msg[i] << 16 | (i + 1 < n ? msg[i + 1] << 8 : 0) | (i + 2 < n ? msg[i + 2] : 0);
        out[o++] = b64[v >> 18 & 63];
        out[o++] = b64[v >> 12 & 63];
        out[o++] = i + 1 < n ? b64[v >> 6 & 63] : '=';
        out[o++] = i + 2 < n ? b64[v & 63] : '=';
    }
    if (size)
        out[o < size ? o : size - 1] = 0;
}

void YtJsonEscape(const char *s, char *out, size_t size)
{
    size_t n = 0;

    for (; *s && n + 7 < size; s++)
    {
        unsigned char c = *s;
        if (c == '"' || c == '\\')
        {
            out[n++] = '\\';
            out[n++] = c;
        }
        else if (c < 0x20)
            n += snprintf(out + n, size - n, "\\u%04x", c);
        else
            out[n++] = c;
    }
    out[n] = 0;
}
