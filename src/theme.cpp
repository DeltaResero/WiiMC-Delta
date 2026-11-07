/****************************************************************************
 * Menu colour themes
 *
 * A slow dark gradient shows bands at 8 bits, and more so from a JPEG, so the
 * background is computed at 8.8 fixed point and dithered down when a theme is
 * chosen. Any colour costs only a table.
 ***************************************************************************/
#include <gccore.h>
#include <string.h>

#include "libwiigui/gui.h"
#include "theme.h"
#include "theme_art.h"

#define THEMES ((int)(sizeof(theme_art) / sizeof(theme_art[0])))
#define THEME_MAX_WIDTH 1024

int ThemeCount()
{
    return THEMES;
}

const char *ThemeName(int index)
{
    return theme_art[index].name;
}

int ThemeFind(const char *name)
{
    for (int i = 0; i < THEMES; i++)
        if (strcmp(theme_art[i].name, name) == 0)
            return i;
    return -1;
}

// Interleaved gradient noise. The copy filter averages three lines, so the
// dither is two levels wide to leave one level after it.
static u8 dither[64][64];
static bool ditherReady = false;

static void InitDither()
{
    if (ditherReady)
        return;
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            float f = 0.06711056f * x + 0.00583715f * y;
            f = 52.9829189f * (f - (int)f);
            dither[y][x] = (u8)((f - (int)f) * 256.0f);
        }
    }
    ditherReady = true;
}

// A position on a grid whose points sit at the centres of n equal cells
static inline void GridPos(int pos, int size, int n, int &index, int &weight)
{
    int p = ((2 * pos + 1) * n * 128) / size - 128; // 8.8
    if (p < 0)
        p = 0;
    index = p >> 8;
    if (index >= n - 1) {
        index = n - 2;
        weight = 256;
    }
    else
        weight = p & 255;
}

void ThemeRenderBackground(GuiImageData *bg, int theme)
{
    u8 *tex = bg->GetImage();
    const int w = bg->GetWidth(), h = bg->GetHeight();
    if (!tex || theme < 0 || theme >= THEMES)
        return;
    const ThemeArt &art = theme_art[theme];

    InitDither();

    if (w > THEME_MAX_WIDTH)
        return;
    static u8 column[THEME_MAX_WIDTH], columnWeight[THEME_MAX_WIDTH];
    for (int x = 0; x < w; x++) {
        int xi, xw;
        GridPos(x, w, THEME_FIELD_W, xi, xw);
        column[x] = xi;
        columnWeight[x] = xw > 255 ? 255 : xw;
    }

    int fieldRow[4][THEME_FIELD_W];
    u8 *t = tex;

    for (int ty = 0; ty < h; ty += 4) {
        for (int line = 0; line < 4; line++) {
            int yi, yw;
            GridPos(ty + line, h, THEME_FIELD_H, yi, yw);
            for (int i = 0; i < THEME_FIELD_W; i++)
                fieldRow[line][i] = (theme_field[yi][i] * (256 - yw) + theme_field[yi + 1][i] * yw) >> 8;
        }

        int gyi, gyw;
        GridPos(ty + 2, h, THEME_GAIN_H, gyi, gyw);

        for (int tx = 0; tx < w; tx += 4, t += 64) {
            // The gains drift slowly, so once per tile is enough
            int gxi, gxw;
            GridPos(tx + 2, w, THEME_GAIN_W, gxi, gxw);
            int gain[3];
            for (int c = 0; c < 3; c++) {
                int top = art.gain[gyi][gxi][c] * (256 - gxw) + art.gain[gyi][gxi + 1][c] * gxw;
                int bottom = art.gain[gyi + 1][gxi][c] * (256 - gxw) + art.gain[gyi + 1][gxi + 1][c] * gxw;
                gain[c] = (top * (256 - gyw) + bottom * gyw) >> 16;
            }

            for (int i = 0; i < 16; i++) {
                const int x = tx + (i & 3), y = ty + (i >> 2);
                const int xi = column[x], xw = columnWeight[x];
                const int *row = fieldRow[i >> 2];
                int v = (row[xi] * (256 - xw) + row[xi + 1] * xw) >> 8;
                v += (t[i * 2 + 1] << 8) / THEME_SPARKLE_SCALE; // the grey art holds the sparkles
                const int d = (dither[y & 63][x & 63] - 128) * 2 + 128;

                u8 out[3];
                for (int c = 0; c < 3; c++) {
                    int o = ((v * gain[c]) >> 8) + d;
                    out[c] = o < 0 ? 0 : (o > 65535 ? 255 : o >> 8);
                }
                t[i * 2] = 255;
                t[i * 2 + 1] = out[0];
                t[32 + i * 2] = out[1];
                t[33 + i * 2] = out[2];
            }
        }
    }
    DCFlushRange(tex, w * h * 4);
}

void ThemeTint(GuiImageData *image, int theme)
{
    u8 *t = image->GetImage();
    if (!t || theme < 0 || theme >= THEMES)
        return;
    const ThemeArt &art = theme_art[theme];
    // Tiles are always whole, so the size may round up
    const int w = (image->GetWidth() + 3) & ~3, h = (image->GetHeight() + 3) & ~3;
    const int bytes = w * h * 4;

    for (int n = 0; n < bytes; n += 64, t += 64) {
        for (int i = 0; i < 16; i++) {
            const int pos = t[i * 2 + 1] * (THEME_RAMP - 1);
            int seg = pos / 255;
            if (seg > THEME_RAMP - 2)
                seg = THEME_RAMP - 2;
            const int f = pos - seg * 255;
            const unsigned char *a = art.ramp[seg], *b = art.ramp[seg + 1];
            t[i * 2 + 1] = (a[0] * (255 - f) + b[0] * f + 127) / 255;
            t[32 + i * 2] = (a[1] * (255 - f) + b[1] * f + 127) / 255;
            t[33 + i * 2] = (a[2] * (255 - f) + b[2] * f + 127) / 255;
        }
    }
    DCFlushRange(image->GetImage(), bytes);
}
