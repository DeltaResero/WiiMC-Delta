/****************************************************************************
 * WiiMC
 *
 * youtube.h
 *
 * Talks to YouTube directly, the way BrewTube does, with RiiTube's links as
 * the fallback for each video
 ***************************************************************************/

#ifndef _YOUTUBE_H_
#define _YOUTUBE_H_

#include "utils/ytparse.h"

// Search results; page is "" for the first page, or the next token from the
// previous page. Returns the number of items, 0 on failure.
int YouTubeSearch(const char *query, const char *page, YtItem *items, int max, char *next, size_t nextsize);

// The videos on a page such as a topic channel or "FEpodcasts_destination"
// (with its params, else NULL), or in a playlist (pass the playlist's ID;
// "VL" is added).
int YouTubeBrowse(const char *browseId, const char *params, YtItem *items, int max);
int YouTubePlaylist(const char *playlistId, YtItem *items, int max);

// Search suggestions for what has been typed so far
int YouTubeSuggest(const char *text, char (*out)[96], int max);

// The 360p link for a video, asked of YouTube on this Wii (the link only
// works from the address that asked for it)
bool YouTubeResolve(const char *videoId, char *url, size_t urlsize, char *reason, size_t reasonsize);

// What YouTubeResolve found for the last video (its captions, audio tracks,
// and height) lasts until YouTubeForget, which any other file calls. The
// serial number changes whenever the captions do.
#define YT_CAPTIONS_MAX 30
void YouTubeForget();
int YouTubeCaptionCount();
int YouTubeCaptionSerial();
const char *YouTubeCaptionName(int i);

// Downloads caption track i and saves it to file as SubRip
bool YouTubeCaptionSave(int i, const char *file);

// The tallest video to ask for, kept in the settings: 360 plays itag 18,
// which has the sound in it; 480 plays separate video and audio streams,
// when the video has them.
void YouTubeSetQuality(int height);
int YouTubeQuality();

// The height of the video YouTubeResolve last found, and the link to its
// sound when that's a stream of its own (NULL when the video has it)
int YouTubeHeight();
const char *YouTubeAudioUrl();

// The video's audio tracks (one per language when it's dubbed), the one
// playing, and the one to play next time it's resolved (0 = the original)
#define YT_AUDIO_MAX 24
int YouTubeAudioCount();
const char *YouTubeAudioName(int i);
int YouTubeAudioPlaying();
void YouTubeSetAudio(int i);

// The video ID from a RiiTube video link, or false when it isn't one
bool YouTubeRiiTubeId(const char *url, char *id, size_t size);

// RiiTube's link for a video, used when YouTube refuses
void YouTubeRiiTubeUrl(const char *videoId, char *url, size_t size);

#endif
