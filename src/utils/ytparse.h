/****************************************************************************
 * WiiMC
 *
 * ytparse.h
 *
 * Reads what WiiMC needs from YouTube's replies
 ***************************************************************************/

#ifndef _YTPARSE_H_
#define _YTPARSE_H_

#include <stddef.h>

typedef struct
{
    char id[64];        // a video ID, or a playlist ID when playlist is set
    char title[256];
    char channel[128];
    char length[24];    // "22:53", "LIVE", or a playlist's "24 videos"
    char views[48];
    char age[48];       // "16 years ago"
    bool playlist;
} YtItem;

// Videos and playlists from a search, browse, or playlist reply, in page order,
// without repeats. next gets the token for the following page, or "".
// Returns the number of items stored.
int YtParseItems(const char *json, size_t len, YtItem *items, int max, char *next, size_t nextsize);

// Search suggestions from the suggestqueries reply. Returns how many.
int YtParseSuggestions(const char *reply, size_t len, char (*out)[96], int max);

// The visitor ID from www.youtube.com/sw.js_data
bool YtParseVisitor(const char *reply, size_t len, char *out, size_t size);

// The link to itag 18 (360p, sound and picture in one file) from a player
// reply. On failure, reason gets YouTube's reason, when it gives one.
bool YtParsePlayer(const char *json, size_t len, char *url, size_t urlsize, char *reason, size_t reasonsize);

typedef struct
{
    char name[64];      // "English", "English (auto-generated)"
    char url[1024];     // timedtext link, about 600 characters
} YtCaption;

// The caption tracks in a player reply, the uploader's own before the
// automatic ones. Returns how many were stored.
int YtParseCaptions(const char *json, size_t len, YtCaption *out, int max);

// Turns YouTube's timedtext reply (format 3: <p t="ms" d="ms">text</p>) into
// SubRip, which MPlayer reads. Each line ends where the next one starts, so
// they don't stack up. Returns the length written to out, or 0 if there was
// no text (out is always terminated).
size_t YtCaptionsToSrt(const char *xml, size_t len, char *out, size_t size);

// Writes s into out as a JSON string's contents, escaped
void YtJsonEscape(const char *s, char *out, size_t size);

#endif
