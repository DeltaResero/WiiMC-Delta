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
#include "settings.h"
#include "utils/http.h"

#define REPLY_SIZE (1024*1024) // enough for a page of results, unpacked

#define YT_API "https://www.youtube.com/youtubei/v1/"
// Each client YouTube is asked as, and the user agent that client's own
// requests carry. A request says the same in its body and its headers.
#define WEB_VERSION "2.20260925.01.00"
#define WEB_UA "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) " \
    "Chrome/156.0.0.0 Safari/537.36"
#define ANDROID_VERSION "21.26.364"
#define ANDROID_UA "com.google.android.youtube/" ANDROID_VERSION " (Linux; U; Android 11) gzip"
#define VISIONOS_UA "Mozilla/5.0 (Macintosh; Intel Mac OS X 15_7_3) AppleWebKit/605.1.15 " \
    "(KHTML, like Gecko) Version/26.0 Safari/605.1.15"

#define WEB_CLIENT "{\"clientName\":\"WEB\",\"clientVersion\":\"" WEB_VERSION "\",\"hl\":\"en\",\"gl\":\"US\""
#define ANDROID_CLIENT "{\"clientName\":\"ANDROID\",\"clientVersion\":\"" ANDROID_VERSION "\"," \
    "\"androidSdkVersion\":30,\"userAgent\":\"" ANDROID_UA "\"," \
    "\"osName\":\"Android\",\"osVersion\":\"11\",\"hl\":\"en\",\"gl\":\"US\""
#define VISIONOS_CLIENT "{\"clientName\":\"VISIONOS\",\"clientVersion\":\"1.02\",\"deviceMake\":\"Apple\"," \
    "\"deviceModel\":\"RealityDevice17,1\",\"userAgent\":\"" VISIONOS_UA "\"," \
    "\"osName\":\"visionOS\",\"osVersion\":\"26.5.23O471\",\"hl\":\"en\",\"gl\":\"US\""

// the browser's headers, for pages that aren't the API; then each client's,
// with the client's number and version where it's known (1 web, 3 Android)
#define BROWSER_HEADERS "User-Agent: " WEB_UA "\r\n"
#define WEB_HEADERS "User-Agent: " WEB_UA "\r\nX-YouTube-Client-Name: 1\r\n" \
    "X-YouTube-Client-Version: " WEB_VERSION "\r\nOrigin: https://www.youtube.com\r\n"
#define ANDROID_HEADERS "User-Agent: " ANDROID_UA "\r\nX-YouTube-Client-Name: 3\r\n" \
    "X-YouTube-Client-Version: " ANDROID_VERSION "\r\n"
#define VISIONOS_HEADERS "User-Agent: " VISIONOS_UA "\r\n"

#define RIITUBE_VIDEO "http://riitube.rc24.xyz/video/wii/?q="
#define RIITUBE_LIST "http://riitube.rc24.xyz/wiimc/"

static char visitor[1024]; // about 450 characters in November 2026

// the captions of the video last resolved
static YtCaption *captions = NULL;
static int captionCount = 0;
static int captionSerial = 0;

// the video's streams: the one found, and the sound
static int height = 0;
static YtAudioTrack *audioTracks = NULL;
static int audioCount = 0;
static int audioPlaying = 0;        // which of audioTracks plays
static char *audioUrl = NULL;       // its link when it's a stream of its own
static char audioWanted[64] = "";   // the track picked for audioFor; "" = original
static char audioFor[32] = "";
static const char *streamAgent = NULL;  // the user agent of the client that gave the links

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
    char *reply = Fetch(url, body, WEB_HEADERS, &len);
    if (!reply)
        return 0;

    int count = YtParseItems(reply, len, items, max, next, nextsize);
    free(reply);
    return count;
}

int YouTubeSearch(const char *query, const char *page, const char *params, YtItem *items, int max,
    char *next, size_t nextsize)
{
    size_t size = 2 * (strlen(query) + strlen(page) + strlen(params)) + 256;
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
        int n = snprintf(body, size, "{\"context\":{\"client\":" WEB_CLIENT "}},\"query\":\"%s\"", esc);
        // base64 needs no escaping
        snprintf(body + n, size - n, params[0] ? ",\"params\":\"%s\"}" : "}", params);
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

// a list from RiiTube, its videos marked as such on their cards
static int RiiTubeItems(const char *url, YtItem *items, int max, char *next, size_t nextsize)
{
    u32 len;
    char *reply = Fetch(url, NULL, NULL, &len);
    if (!reply)
        return 0;

    int count = YtParseRiiTube(reply, len, items, max, next, nextsize);
    free(reply);

    for (int i = 0; i < count; i++)
        snprintf(items[i].channel, sizeof(items[i].channel), "via RiiTube");
    return count;
}

int RiiTubeSearch(const char *query, const char *page, YtItem *items, int max, char *next, size_t nextsize)
{
    // a later page is the link RiiTube gave for it
    if (page[0])
        return strncmp(page, RIITUBE_LIST, strlen(RIITUBE_LIST)) == 0 ?
            RiiTubeItems(page, items, max, next, nextsize) : 0;

    size_t size = strlen(RIITUBE_LIST) + 4 + 3 * strlen(query) + 1;
    char *url = (char *)malloc(size);
    if (!url)
        return 0;

    char *w = url + sprintf(url, RIITUBE_LIST "?q=");
    for (const unsigned char *r = (const unsigned char *)query; *r; r++)
    {
        if ((*r >= 'A' && *r <= 'Z') || (*r >= 'a' && *r <= 'z') || (*r >= '0' && *r <= '9') ||
            *r == '-' || *r == '_' || *r == '.' || *r == '~')
            *w++ = *r;
        else if (*r == ' ')
            *w++ = '+';
        else
            w += sprintf(w, "%%%02X", *r);
    }
    *w = 0;

    int count = RiiTubeItems(url, items, max, next, nextsize);
    free(url);
    return count;
}

bool YouTubeUseRiiTube()
{
    return WiiSettings.youtubeSource == 1;
}

void YouTubeSetRiiTube(bool on)
{
    WiiSettings.youtubeSource = on ? 1 : 0; // kept in settings.xml
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

// the player reply for a video as a client sees it; NULL on failure
static char *Player(const char *videoId, const char *client, const char *headers, u32 *len)
{
    char id[32], vis[1024], body[2560];

    YtJsonEscape(videoId, id, sizeof(id));
    YtJsonEscape(Visitor(), vis, sizeof(vis));
    snprintf(body, sizeof(body), "{\"videoId\":\"%s\",\"context\":{\"client\":%s%s%s%s}},"
        "\"contentCheckOk\":true,\"racyCheckOk\":true}", id, client,
        vis[0] ? ",\"visitorData\":\"" : "", vis, vis[0] ? "\"" : "");
    return Fetch(YT_API "player?prettyPrint=false", body, headers, len);
}

static bool Playable(const char *reply)
{
    return strstr(reply, "\"playabilityStatus\":{\"status\":\"OK\"") != NULL;
}

// which of the tracks to play: the one picked for this video, else the original
static int AudioPick()
{
    for (int i = 0; audioWanted[0] && i < audioCount; i++)
        if (strcmp(audioTracks[i].name, audioWanted) == 0)
            return i;
    return 0;
}

bool YouTubeResolve(const char *videoId, char *url, size_t urlsize, char *reason, size_t reasonsize)
{
    u32 len;

    url[0] = 0;
    if (reason && reasonsize)
        reason[0] = 0;

    YouTubeForget();

    if (strcmp(audioFor, videoId) != 0)
    {
        // a new video: its original sound, until something else is picked
        snprintf(audioFor, sizeof(audioFor), "%s", videoId);
        audioWanted[0] = 0;
    }

    if (!captions)
        captions = (YtCaption *)malloc(YT_CAPTIONS_MAX * sizeof(YtCaption));
    if (!audioTracks)
        audioTracks = (YtAudioTrack *)malloc(YT_AUDIO_MAX * sizeof(YtAudioTrack));
    if (!captions || !audioTracks)
        return false;

    // Taller than 360p, or another language, comes as separate video and
    // audio streams, which the VISIONOS client gives plain links to. At 360p
    // with the original sound, itag 18 has both in one file: better.
    int quality = WiiSettings.youtubeQuality;
    if (quality > 360 || audioWanted[0])
    {
        char *reply = Player(videoId, VISIONOS_CLIENT, VISIONOS_HEADERS, &len);
        if (reply && Playable(reply))
        {
            int h = 0;
            audioCount = YtParseAdaptive(reply, len, quality, url, urlsize, &h, audioTracks, YT_AUDIO_MAX);
            int pick = AudioPick();

            if (url[0] && audioCount > 0 && audioTracks[pick].url[0] && (h > 360 || pick > 0))
            {
                audioUrl = strdup(audioTracks[pick].url);
                if (audioUrl)
                {
                    audioPlaying = pick;
                    height = h;
                    captionCount = YtParseCaptions(reply, len, captions, YT_CAPTIONS_MAX);
                    streamAgent = VISIONOS_UA;
                    free(reply);
                    return true;
                }
            }
        }
        free(reply);
        url[0] = 0;
        audioCount = 0;
    }

    char *reply = Player(videoId, ANDROID_CLIENT, ANDROID_HEADERS, &len);
    if (!reply)
        return false;

    bool ok = YtParsePlayer(reply, len, url, urlsize, reason, reasonsize);
    if (ok)
    {
        // the languages there are, for the player settings; their links come
        // with the VISIONOS reply when one is picked
        char none[8];
        int h;
        audioCount = YtParseAdaptive(reply, len, 0, none, sizeof(none), &h, audioTracks, YT_AUDIO_MAX);
        height = 360;
        captionCount = YtParseCaptions(reply, len, captions, YT_CAPTIONS_MAX);
        streamAgent = ANDROID_UA;
    }
    free(reply);
    return ok;
}

void YouTubeSetQuality(int h)
{
    WiiSettings.youtubeQuality = h; // kept in settings.xml
}

int YouTubeQuality()
{
    return WiiSettings.youtubeQuality;
}

int YouTubeHeight()
{
    return height;
}

const char *YouTubeAudioUrl()
{
    return audioUrl;
}

int YouTubeAudioCount()
{
    return audioCount;
}

const char *YouTubeAudioName(int i)
{
    return i >= 0 && i < audioCount ? audioTracks[i].name : "";
}

int YouTubeAudioPlaying()
{
    return audioPlaying;
}

void YouTubeSetAudio(int i)
{
    if (i <= 0 || i >= audioCount)
        audioWanted[0] = 0;
    else
        snprintf(audioWanted, sizeof(audioWanted), "%s", audioTracks[i].name);
}

void YouTubeForget()
{
    captionCount = 0;
    captionSerial++;
    audioCount = 0;
    audioPlaying = 0;
    free(audioUrl);
    audioUrl = NULL;
    height = 0;
    streamAgent = NULL;
}

// The user agent to fetch the video last resolved with, as the client that
// gave its links would; NULL for anything else. FFmpeg's https asks this.
extern "C" const char *YouTubeStreamAgent(void)
{
    return streamAgent;
}

int YouTubeCaptionCount()
{
    return captionCount;
}

int YouTubeCaptionSerial()
{
    return captionSerial;
}

const char *YouTubeCaptionName(int i)
{
    return i >= 0 && i < captionCount ? captions[i].name : "";
}

bool YouTubeCaptionSave(int i, const char *file)
{
    if (i < 0 || i >= captionCount)
        return false;

    u32 len;
    char headers[256];
    snprintf(headers, sizeof(headers), "User-Agent: %s\r\n", streamAgent ? streamAgent : WEB_UA);
    char *xml = Fetch(captions[i].url, NULL, headers, &len);
    if (!xml)
        return false;

    // SubRip takes about as much room as YouTube's XML; leave plenty
    size_t size = len * 2 + 4096;
    char *srt = (char *)malloc(size);
    size_t n = srt ? YtCaptionsToSrt(xml, len, srt, size) : 0;
    free(xml);

    bool ok = false;
    if (n > 0)
    {
        FILE *f = fopen(file, "wb");
        if (f)
        {
            ok = fwrite(srt, 1, n, f) == n;
            if (fclose(f) != 0)
                ok = false;
        }
    }
    free(srt);
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
