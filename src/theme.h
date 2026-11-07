/****************************************************************************
 * Menu colour themes: the background and small images, built from one set of
 * grey art.
 ***************************************************************************/
#ifndef _THEME_H_
#define _THEME_H_

class GuiImageData;

int ThemeCount();
const char *ThemeName(int index);
int ThemeFind(const char *name);

// Puts the theme's colours into the images. Each must already hold its grey art.
void ThemeRenderBackground(GuiImageData *bg, int theme);
void ThemeTint(GuiImageData *image, int theme);

#endif
