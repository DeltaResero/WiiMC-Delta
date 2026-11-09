/****************************************************************************
 * WiiMC
 *
 * youtube.cpp
 *
 * Talks to YouTube directly, the way BrewTube (ReviveMii, GPL-3.0) does:
 * search, topic pages, and playlists through the web client's API, and each
 * video's link through the Android client, which still hands out the 360p
 * file (itag 18) without JavaScript. RiiTube's links stay as the fallback.
 ***************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gccore.h>

#include "youtube.h"
#include "utils/http.h"

#define REPLY_SIZE (1024*1024) // enough for a page of results, unpacked

#define YT_API "https://www.youtube.com/youtubei/v1/"
#define WEB_CLIENT "{\"clientName\":\"WEB\",\"clientVersion\":\"2.20260925.01.00\",\"hl\":\"en\",\"gl\":\"US\""
#define ANDROID_CLIENT "{\"clientName\":\"ANDROID\",\"clientVersion\":\"21.26.364\",\"androidSdkVersion\":30," \
    "\"userAgent\":\"com.google.android.youtube/21.26.364 (Linux; U; Android 11) gzip\"," \
    "\"osName\":\"Android\",\"osVersion\":\"11\",\"hl\":\"en\",\"gl\":\"US\""
#define BROWSER_HEADERS "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 " \
    "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36\r\n"

#define RIITUBE_VIDEO "http://riitube.rc24.xyz/video/wii/?q="

static char visitor[1024]; // about 450 characters in November 2026

// a reply from YouTube in a new buffer, which the caller frees; NULL on failure
static char *Fetch(const char *url, const char *body, const char *headers, u32 *len)
{
    char *reply = (char *)malloc(REPLY_SIZE + 1);
    if (!reply)
        return NULL;

    *len = http_fetch(url, body, headers, reply, REPLY_SIZE);
    if (*len == 0)
    {
        free(reply);
        return NULL;
    }
    reply[*len] = 0;
    return reply;
}

// YouTube refuses the player without a visitor ID, which its service worker
// data hands out
static const char *Visitor()
{
    if (visitor[0])
        return visitor;

    u32 len;
    char *reply = Fetch("https://www.youtube.com/sw.js_data", NULL,
        BROWSER_HEADERS "Origin: https://www.youtube.com\r\nReferer: https://www.youtube.com/\r\n", &len);

    if (reply)
    {
        YtParseVisitor(reply, len, visitor, sizeof(visitor));
        free(reply);
    }
    return visitor;
}

static int Items(const char *endpoint, const char *body, YtItem *items, int max, char *next, size_t nextsize)
{
    char url[128];
    snprintf(url, sizeof(url), YT_API "%s?prettyPrint=false", endpoint);

    u32 len;
    char *reply = Fetch(url, body, BROWSER_HEADERS, &len);
    if (!reply)
        return 0;

    int count = YtParseItems(reply, len, items, max, next, nextsize);
    free(reply);
    return count;
}

int YouTubeSearch(const char *query, const char *page, YtItem *items, int max, char *next, size_t nextsize)
{
    size_t size = 2 * (strlen(query) + strlen(page)) + 256;
    char *body = (char *)malloc(size);
    char *esc = (char *)malloc(size);
    if (!body || !esc)
    {
        free(body);
        free(esc);
        return 0;
    }

    if (page[0])
    {
        YtJsonEscape(page, esc, size);
        snprintf(body, size, "{\"context\":{\"client\":" WEB_CLIENT "}},\"continuation\":\"%s\"}", esc);
    }
    else
    {
        YtJsonEscape(query, esc, size);
        snprintf(body, size, "{\"context\":{\"client\":" WEB_CLIENT "}},\"query\":\"%s\"}", esc);
    }

    int count = Items("search", body, items, max, next, nextsize);
    free(body);
    free(esc);
    return count;
}

int YouTubeBrowse(const char *browseId, const char *params, YtItem *items, int max)
{
    char esc[160], body[512];

    YtJsonEscape(browseId, esc, sizeof(esc));
    // params are base64, which needs no escaping
    snprintf(body, sizeof(body), "{\"context\":{\"client\":" WEB_CLIENT "}},\"browseId\":\"%s\"%s%s%s}", esc,
        params ? ",\"params\":\"" : "", params ? params : "", params ? "\"" : "");
    return Items("browse", body, items, max, NULL, 0);
}

int YouTubePlaylist(const char *playlistId, YtItem *items, int max)
{
    char id[80];
    snprintf(id, sizeof(id), "VL%s", playlistId);
    return YouTubeBrowse(id, NULL, items, max);
}

int YouTubeSuggest(const char *text, char (*out)[96], int max)
{
    char url[512];
    int n = snprintf(url, sizeof(url), "https://suggestqueries-clients6.youtube.com/complete/search"
        "?ds=yt&hl=en&gl=us&client=youtube&gs_ri=youtube&ie=utf-8&oe=utf-8&q=");

    // percent-encode everything but letters and digits
    for (const unsigned char *c = (const unsigned char *)text; *c && n + 4 < (int)sizeof(url); c++)
    {
        if ((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9'))
            url[n++] = *c;
        else
            n += snprintf(url + n, sizeof(url) - n, "%%%02X", *c);
    }
    url[n] = 0;

    u32 len;
    char *reply = Fetch(url, NULL, BROWSER_HEADERS, &len);
    if (!reply)
        return 0;

    int count = YtParseSuggestions(reply, len, out, max);
    free(reply);
    return count;
}

bool YouTubeResolve(const char *videoId, char *url, size_t urlsize, char *reason, size_t reasonsize)
{
    char id[32], vis[1024], body[2048];

    url[0] = 0;
    if (reason && reasonsize)
        reason[0] = 0;

    YtJsonEscape(videoId, id, sizeof(id));
    YtJsonEscape(Visitor(), vis, sizeof(vis));
    snprintf(body, sizeof(body), "{\"videoId\":\"%s\",\"context\":{\"client\":" ANDROID_CLIENT "%s%s%s}},"
        "\"contentCheckOk\":true,\"racyCheckOk\":true}", id,
        vis[0] ? ",\"visitorData\":\"" : "", vis, vis[0] ? "\"" : "");

    u32 len;
    char *reply = Fetch(YT_API "player?prettyPrint=false", body, BROWSER_HEADERS, &len);
    if (!reply)
        return false;

    bool ok = YtParsePlayer(reply, len, url, urlsize, reason, reasonsize);
    free(reply);
    return ok;
}

bool YouTubeRiiTubeId(const char *url, char *id, size_t size)
{
    size_t n = strlen(RIITUBE_VIDEO);

    if (strncmp(url, RIITUBE_VIDEO, n) != 0)
        return false;

    url += n;
    size_t len = strcspn(url, "&#");
    if (len == 0 || len >= size)
        return false;

    memcpy(id, url, len);
    id[len] = 0;
    return true;
}

void YouTubeRiiTubeUrl(const char *videoId, char *url, size_t size)
{
    snprintf(url, size, RIITUBE_VIDEO "%s", videoId);
}
