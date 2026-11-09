/*
   vo_gekko.c - MPlayer video driver for Wii

   Copyright (C) 2008 dhewg
   Copyright (C) 2008 sepp256 - YUV frame rendering functions

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, write to the
   Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301 USA.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <malloc.h>

#include "config.h"
#include "mp_msg.h"
#include "subopt-helper.h"
#include "help_mp.h"
#include "video_out.h"
#include "video_out_internal.h"
#include "aspect.h"
#include "mp_fifo.h"
#include "sub/sub.h"
#include "osdep/keycodes.h"
#include "osdep/gx_supp.h"
#include "fastmemcpy.h"
#include "csputils.h"
#include "../../utils/mem2_manager.h"

#include <gccore.h>

void ShutdownGui();
void SetMPlayerSettings();

extern int mplayerwidth;
extern int mplayerheight;

static const vo_info_t info = {
    "gekko video output",
    "gekko",
    "Team Twiizers",
    ""
};

const LIBVO_EXTERN (gekko)

static u16 pitch[3];
static u16 image_width = 0, image_height = 0;
static u16 chroma_width, chroma_height;

u32 gx_width, gx_height;

// A decoded frame waits here, out of the texture, until the player knows
// how long until it shows. If that's long, as for a still in a variable
// frame rate video, the texture keeps the frame on screen, so the player
// can redraw it, and the controls drawn over it, while it waits. The
// decoder keeps the frame's planes until it decodes the next one, which
// comes only after this one shows.
static uint8_t *held_planes[3];
static int held_stride[3];
static int frame_held;   // a decoded frame not yet in the texture
static int osd_held;     // its OSD and subtitles, still to draw
static int frame_unshown; // the texture holds a frame not yet shown

static void upload_held_frame(void)
{
    if (!frame_held)
        return;
    GX_FillTextureYUV(held_planes, held_stride);
    frame_held = 0;
    frame_unshown = 1;
    if (osd_held) {
        vo_draw_text(image_width, image_height, vo_draw_alpha_gekko);
        osd_held = 0;
    }
}

// The frame will show soon: put it in the texture now, as before.
void wiiUploadFrame(void)
{
    upload_held_frame();
}

// Draws the frame on screen again, with the controls over it. Returns 0,
// drawing nothing, when the texture holds a frame that isn't due yet.
int wiiRedrawShownFrame(void)
{
    if (frame_unshown || !image_width)
        return 0;
    DrawMPlayer();
    return 1;
}

static int draw_slice(uint8_t *image[], int stride[], int w, int h, int x, int y)
{
    GX_FillTextureYUV(image, stride);
    frame_unshown = 1;
    return 0;
}

static void draw_osd(void)
{
    if (frame_held)
        osd_held = 1;
    else
        vo_draw_text(image_width, image_height, vo_draw_alpha_gekko);
}

static uint32_t draw_image(mp_image_t *mpi)
{
    if (mpi->flags & MP_IMGFLAG_PLANAR) {
        for (int i = 0; i < 3; i++) {
            held_planes[i] = mpi->planes[i];
            held_stride[i] = mpi->stride[i];
        }
        frame_held = 1;
        osd_held = 0;
    }

    return VO_TRUE;
}

static void flip_page(void)
{
    upload_held_frame();
    GX_RenderTexture();
    frame_unshown = 0;
}

static int draw_frame(uint8_t *src[])
{
    return VO_ERROR;
}

static int inline query_format(uint32_t format)
{
    // Accept any 8-bit planar YUV format. Formats with more bits per sample
    // (10-bit VP9, HEVC and AV1) go through the scale filter first: the GX
    // textures take one byte per sample.
    if (mp_get_chroma_shift(format, NULL, NULL, NULL) && !IMGFMT_IS_YUVP16(format))
        return VFCAP_CSP_SUPPORTED | VFCAP_CSP_SUPPORTED_BY_HW | VFCAP_OSD | VFCAP_HWSCALE_UP | VFCAP_HWSCALE_DOWN | VFCAP_ACCEPT_STRIDE | VOCAP_NOSLICES;
    else return VO_FALSE;
}

#define WIDTH_WIDE 854

void wiiSetFullScreen()
{
    mplayerwidth = WIDTH_WIDE;
}

void wiiSetScreenNorm()
{
    if (CONF_GetAspectRatio() != CONF_ASPECT_16_9)
        mplayerwidth = vmode->fbWidth;
}

void reinit_video()
{
    ShutdownGui(); // tell GUI to shut down, MPlayer is ready to take over
    SetMPlayerSettings(); // pass settings from WiiMC into MPlayer

    GX_StartYUV(image_width, image_height, gx_width / 2, gx_height / 2 );
    GX_ConfigTextureYUV(image_width, image_height, chroma_width, chroma_height);
}

static int config(uint32_t width, uint32_t height, uint32_t d_width,
          uint32_t d_height, uint32_t flags, char *title,
          uint32_t format)
{
    image_width = width;
    image_height = height;
    frame_held = osd_held = frame_unshown = 0;

    float screen_aspect = (float)mplayerwidth / (float)mplayerheight;
    float image_aspect = (float)d_width / (float)d_height;

    if (image_aspect > screen_aspect)
    {
        gx_width = mplayerwidth;
        gx_height = (f32)d_height * ((f32)mplayerwidth / (f32)d_width);
    }
    else
    {
        gx_width = (f32)d_width * ((f32)mplayerheight / (f32)d_height);
        gx_height = mplayerheight;
    }

    vo_screenheight = image_height;
    vo_screenwidth = image_width;

    int xs, ys;
    mp_get_chroma_shift(format, &xs, &ys, NULL);
    chroma_width=image_width >> xs;
    chroma_height=image_height >> ys;
    reinit_video();
    return 0;
}

static void uninit(void)
{
    image_width = 0;
    image_height = 0;
    frame_held = osd_held = frame_unshown = 0;
}

static void check_events(void)
{
}

static int valid_csp(void *p)
{
    int *csp = p;
    return *csp > 0 && *csp < MP_CSP_XYZ;
}

extern int colorspace;
extern int levelconv;

static const opt_t subopts[] = {
    {"colorspace", OPT_ARG_INT,  &colorspace, valid_csp},
    {"levelconv",  OPT_ARG_BOOL, &levelconv,  NULL},
    {NULL}
};


static int preinit(const char *arg)
{
    colorspace = MP_CSP_DEFAULT;
    levelconv = 1;

    subopt_parse(arg, subopts);

    if (CONF_GetAspectRatio() == CONF_ASPECT_16_9)
        mplayerwidth = WIDTH_WIDE; // 480 * 16 / 9

    vo_screenheight = mplayerheight;
    vo_screenwidth = mplayerwidth;
    vo_fs=1;

    return 0;
}

static int control(uint32_t request, void *data)
{
    switch (request)
    {
        case VOCTRL_QUERY_FORMAT:
            return query_format(*((uint32_t *)data));
        case VOCTRL_UPDATE_SCREENINFO:
            vo_screenwidth = mplayerheight;
            vo_screenheight = mplayerwidth;
            aspect_save_screenres(vo_screenwidth, vo_screenheight);
            return VO_TRUE;

        case VOCTRL_DRAW_IMAGE:
            return draw_image(data);
        case VOCTRL_RESET:
            // a seek: the decoder lets go of a held frame's planes
            frame_held = osd_held = 0;
            return VO_TRUE;
        default:
            return VO_NOTIMPL;
    }
}
