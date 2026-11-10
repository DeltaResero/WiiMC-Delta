/****************************************************************************
 * WiiMC
 *
 * youtube_menu.h
 *
 * The YouTube screen: search with suggestions, topic tiles, and results
 ***************************************************************************/

#ifndef _YOUTUBE_MENU_H_
#define _YOUTUBE_MENU_H_

#include <stddef.h>

// Runs the YouTube screen over the browser. Returns true when a video was
// picked: url gets its RiiTube link (WiiMC asks YouTube for the real one when
// it plays) and title its name. Returns false when the user backed out.
bool YouTubeScreen(char *url, size_t urlsize, char *title, size_t titlesize);

// True once after a video was picked, so the browser can reopen the screen,
// with the same results, when the video ends
bool YouTubeScreenReopen();

#endif
