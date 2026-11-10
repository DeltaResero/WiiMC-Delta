/****************************************************************************
 * WiiMC
 * Tantric 2009-2012
 *
 * input.cpp
 * Wii/GameCube controller management
 ***************************************************************************/

#include <gccore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ogcsys.h>
#include <unistd.h>
#include <wiiuse/wpad.h>
#include <ogc/lwp_watchdog.h>
#include <wiikeyboard/keyboard.h>

#include "wiimc.h"
#include "menu.h"
#include "video.h"
#include "input.h"
#include "settings.h"
#include "libwiigui/gui.h"
#include "input/input.h"
#include "osdep/gx_supp.h"
#include "utils/3ds.h"

#define RUMBLE_MAX              60000
#define RUMBLE_COOLOFF          10000000
#define VOL_DELAY               30000
#define VOLDISP_MAX             500000

#define RESIZE_INITIAL_DELAY    500000 // to allow more precise adjustment
#define RESIZE_DELAY            5000

static bool rumbleDisabled = false;
static int rumbleOn[4] = {0,0,0,0};
static u64 timeprev[4];
static u64 now[4];
static int osdLevel = 0;
static int volprev = 0, volnow = 0;
static int resizeprev = 0, resizeinitial = 0;

extern bool want3DS;

GuiTrigger userInput[4];

/****************************************************************************
 * USB keyboard
 *
 * Outside a text box the keyboard acts as GameCube controller 1: the arrows
 * are the d-pad, Enter and Space are A, Esc and Backspace are B, Tab is R
 * (Shift+Tab is L), and + and - are Y and X. While a video plays, Esc and
 * Backspace leave it like Start and Tab shows its info like B.
 ***************************************************************************/

static bool kbdTyping = false;
static keyboard_event kbdQueue[32];
static int kbdHead = 0, kbdTail = 0;
static u16 kbdHeldGui[256], kbdHeldPlay[256]; // by keycode, as pressed
static u16 kbdDownGui = 0, kbdDownPlay = 0;
static u16 kbdPadDown = 0, kbdPadHeld = 0;

static void KeyboardButtons(const keyboard_event &ev, u16 &gui, u16 &play)
{
    bool shift = MOD_ONESET(ev.modifiers, MOD_SHIFT_L | MOD_SHIFT_R);
    gui = play = 0;

    switch(ev.symbol)
    {
        case KS_Up: case KS_KP_Up:          gui = play = PAD_BUTTON_UP; break;
        case KS_Down: case KS_KP_Down:      gui = play = PAD_BUTTON_DOWN; break;
        case KS_Left: case KS_KP_Left:      gui = play = PAD_BUTTON_LEFT; break;
        case KS_Right: case KS_KP_Right:    gui = play = PAD_BUTTON_RIGHT; break;
        case KS_Return: case KS_KP_Enter:
        case KS_space:                      gui = play = PAD_BUTTON_A; break;
        case KS_Escape: case KS_BackSpace:  gui = play = PAD_BUTTON_B; break;
        case KS_Tab:                        gui = play = shift ? PAD_TRIGGER_L : PAD_TRIGGER_R; break;
        case KS_plus: case KS_equal:
        case KS_KP_Add:                     gui = play = PAD_BUTTON_Y; break;
        case KS_minus: case KS_KP_Subtract: gui = play = PAD_BUTTON_X; break;
    }
}

static void ReadKeyboard()
{
    keyboard_event ev;

    kbdDownGui = kbdDownPlay = 0;

    while(KEYBOARD_GetEvent(&ev) > 0)
    {
        if(ev.type == KEYBOARD_DISCONNECTED)
        {
            memset(kbdHeldGui, 0, sizeof(kbdHeldGui));
            memset(kbdHeldPlay, 0, sizeof(kbdHeldPlay));
            continue;
        }

        if(ev.type == KEYBOARD_RELEASED)
        {
            // by keycode: Shift can change the symbol before the key comes up
            kbdHeldGui[ev.keycode] = kbdHeldPlay[ev.keycode] = 0;
            continue;
        }

        if(ev.type != KEYBOARD_PRESSED)
            continue;

        if(kbdTyping)
        {
            if(((kbdTail + 1) & 31) != kbdHead)
            {
                kbdQueue[kbdTail] = ev;
                kbdTail = (kbdTail + 1) & 31;
            }
            continue;
        }

        if(MOD_ONESET(ev.modifiers, MOD_ANYCONTROL | MOD_ANYMETA))
            continue;

        u16 gui, play;
        KeyboardButtons(ev, gui, play);
        kbdHeldGui[ev.keycode] = gui;
        kbdHeldPlay[ev.keycode] = play;
        kbdDownGui |= gui;
        kbdDownPlay |= play;
    }
}

static u16 KeyboardHeld(const u16 *held)
{
    u16 b = 0;

    for(int i = 0; i < 256; i++)
        b |= held[i];

    return b;
}

// a text box takes the keys while it's open
void KeyboardTyping(bool on)
{
    kbdTyping = on;
    kbdHead = kbdTail = 0;
    memset(kbdHeldGui, 0, sizeof(kbdHeldGui));
    memset(kbdHeldPlay, 0, sizeof(kbdHeldPlay));
}

bool KeyboardGetKey(keyboard_event *ev)
{
    if(kbdHead == kbdTail)
        return false;

    *ev = kbdQueue[kbdHead];
    kbdHead = (kbdHead + 1) & 31;
    return true;
}

/****************************************************************************
 * UpdatePads
 *
 * Scans wpad
 ***************************************************************************/
void UpdatePads(bool playing)
{
    WPAD_ReadPending(0, NULL); // only wiimote 1
    PAD_ScanPads();
    ReadKeyboard();

    for(int i=3; i >= 0; i--)
    {
        userInput[i].pad.btns_d = PAD_ButtonsDown(i);
        userInput[i].pad.btns_h = PAD_ButtonsHeld(i);
        userInput[i].pad.stickX = PAD_StickX(i);
        userInput[i].pad.stickY = PAD_StickY(i);
    }

    if(want3DS)
        CTRScanPads();
    userInput[0].cpad.data.down = ctr.data.down;
    userInput[0].cpad.data.held = ctr.data.held;
    userInput[0].cpad.data.stick.x = ctr.data.stick.x;
    userInput[0].cpad.data.stick.y = ctr.data.stick.y;

    kbdPadDown = playing ? kbdDownPlay : kbdDownGui;
    kbdPadHeld = KeyboardHeld(playing ? kbdHeldPlay : kbdHeldGui);
    userInput[0].pad.btns_d |= kbdPadDown;
    userInput[0].pad.btns_h |= kbdPadHeld;
}

/****************************************************************************
 * SetupPads
 *
 * Sets up userInput triggers for use
 ***************************************************************************/
void SetupPads()
{
    PAD_Init();
    KEYBOARD_Init(NULL); // its thread costs about 0.02% of the CPU with no keyboard in

    WPAD_SetIdleTimeout(60);

    // read wiimote accelerometer and IR data
    WPAD_SetDataFormat(WPAD_CHAN_ALL, WPAD_FMT_BTNS_ACC_IR);
    WPAD_SetVRes(WPAD_CHAN_ALL, screenwidth, screenheight);

    for(int i=0; i < 4; i++)
    {
        userInput[i].chan = i;
        userInput[i].wpad = WPAD_Data(i);
    }
}

/****************************************************************************
 * ShutoffRumble
 ***************************************************************************/

static void ShutoffRumble(int i, int cooloff)
{
    if(CONF_GetPadMotorMode() == 0)
        return;

    timeprev[i] = gettime() + cooloff;
    WPAD_Rumble(i, 0); // rumble off
    rumbleOn[i] = 0;
}

void ShutoffRumble()
{
    ShutoffRumble(0, RUMBLE_COOLOFF*3);
}

void DisableRumble()
{
    rumbleDisabled = true;
    ShutoffRumble();
}

void EnableRumble()
{
    rumbleDisabled = false;
}

void RequestRumble(int i)
{
    if(CONF_GetPadMotorMode() == 0 || !WiiSettings.rumble || rumbleDisabled || i < 0)
        return;

    now[i] = gettime();

    if(timeprev[i] > now[i])
        return;

    if(diff_usec(timeprev[i], now[i]) > RUMBLE_MAX)
    {
        rumbleOn[i] = 1;
        WPAD_Rumble(i, 1); // rumble on
        timeprev[i] = now[i];
    }
}

/****************************************************************************
 * DoRumble
 ***************************************************************************/

void DoRumble(int i)
{
    if(rumbleOn[i])
    {
        now[i] = gettime();

        if(diff_usec(timeprev[i], now[i]) > RUMBLE_MAX)
            ShutoffRumble(i, RUMBLE_COOLOFF);
    }
}

/****************************************************************************
 * MPlayerInput
 ***************************************************************************/

void MPlayerResize(float fZoomHorIncr, float fZoomVertIncr)
{
    WiiSettings.videoZoomHor += fZoomHorIncr;
    WiiSettings.videoZoomVert += fZoomVertIncr;
    GX_SetScreenPos(WiiSettings.videoXshift, WiiSettings.videoYshift,
        CONF_GetAspectRatio() == CONF_ASPECT_4_3 ? WiiSettings.videoFull ? WiiSettings.videoZoomHor * 1.335
            : WiiSettings.videoZoomHor : WiiSettings.videoZoomHor, WiiSettings.videoZoomVert);
}

bool point_on;

// GC rumble, cool idea to use subtitles to add rumble to movies
#if 0
bool timerRumbleEnable = false;
bool rumbleRandEnable = false;
unsigned timerRumble = 0;
#endif

u8 cur_chan = 0;

void MPlayerInput()
{
    bool ir = false;
    bool inDVDMenu = wiiInDVDMenu();
    static bool volumeUpdated = false;

    if(userInput[0].wpad->ir.valid) {
        ir = true;
        point_on = true;
        //printf("test pointer: %d", point_on);
    } else
        point_on = false;

    // The A button is used to switch between port 1 and 2, menu enables all
    u16 down = PAD_ButtonsDown(cur_chan);
    u16 held = PAD_ButtonsHeld(cur_chan);
    if(cur_chan == 0 && userInput[1].pad.btns_d == PAD_BUTTON_A) {
        cur_chan = 1;
        down = PAD_ButtonsDown(cur_chan);
        held = PAD_ButtonsHeld(cur_chan);
    } else if(cur_chan == 1 && userInput[0].pad.btns_d == PAD_BUTTON_A) {
        cur_chan = 0;
        down = PAD_ButtonsDown(cur_chan);
        held = PAD_ButtonsHeld(cur_chan);
    }

    if(cur_chan == 0)
    {
        down |= kbdPadDown;
        held |= kbdPadHeld;
    }

    // The Wii Remote's buttons, with a Classic Controller's counted as the
    // ones that do the same: X as 1, Y as 2, and ZR as B held.
    const u32 remote = WPAD_BUTTON_A | WPAD_BUTTON_B | WPAD_BUTTON_1 | WPAD_BUTTON_2 | WPAD_BUTTON_PLUS |
        WPAD_BUTTON_MINUS | WPAD_BUTTON_HOME | WPAD_BUTTON_UP | WPAD_BUTTON_DOWN | WPAD_BUTTON_LEFT |
        WPAD_BUTTON_RIGHT;
    u32 wd = userInput[0].wpad->btns_d & remote;
    u32 wh = userInput[0].wpad->btns_h & remote;
    bool back = false;

    if(userInput[0].wpad->exp.type == WPAD_EXP_CLASSIC)
    {
        static const u32 same[][2] = {
            { WPAD_CLASSIC_BUTTON_A, WPAD_BUTTON_A }, { WPAD_CLASSIC_BUTTON_X, WPAD_BUTTON_1 },
            { WPAD_CLASSIC_BUTTON_Y, WPAD_BUTTON_2 }, { WPAD_CLASSIC_BUTTON_PLUS, WPAD_BUTTON_PLUS },
            { WPAD_CLASSIC_BUTTON_MINUS, WPAD_BUTTON_MINUS }, { WPAD_CLASSIC_BUTTON_HOME, WPAD_BUTTON_HOME },
            { WPAD_CLASSIC_BUTTON_UP, WPAD_BUTTON_UP }, { WPAD_CLASSIC_BUTTON_DOWN, WPAD_BUTTON_DOWN },
            { WPAD_CLASSIC_BUTTON_LEFT, WPAD_BUTTON_LEFT }, { WPAD_CLASSIC_BUTTON_RIGHT, WPAD_BUTTON_RIGHT },
            { WPAD_CLASSIC_BUTTON_ZR, WPAD_BUTTON_B }
        };
        for(unsigned i = 0; i < sizeof(same) / sizeof(same[0]); i++)
        {
            if(userInput[0].wpad->btns_d & same[i][0]) wd |= same[i][1];
            if(userInput[0].wpad->btns_h & same[i][0]) wh |= same[i][1];
        }
        if(userInput[0].wpad->btns_d & WPAD_CLASSIC_BUTTON_B)
            back = true;
    }

    // B goes back, as in the menus. The Wii Remote's B is also held with
    // the d-pad to size the picture, so it goes back when it's let go
    // without the d-pad having been used.
    static bool bHeldAlone = false;
    const u32 dpad = WPAD_BUTTON_UP | WPAD_BUTTON_DOWN | WPAD_BUTTON_LEFT | WPAD_BUTTON_RIGHT;
    if(userInput[0].wpad->btns_d & WPAD_BUTTON_B)
        bHeldAlone = true;
    if(wh & dpad)
        bHeldAlone = false;
    if(userInput[0].wpad->btns_u & WPAD_BUTTON_B && bHeldAlone)
    {
        bHeldAlone = false;
        back = true;
    }
    if(down & PAD_BUTTON_B || userInput[0].cpad.data.down & CTR_BUTTON_B)
        back = true;

    bool home = wd & WPAD_BUTTON_HOME || down & PAD_BUTTON_START || userInput[0].cpad.data.down & CTR_BUTTON_START;

    if(back && VideoSettingsVisible())
    {
        VideoSettingsBack();
        back = false;
    }

    // 1 shows or hides the bar, and 2 the player settings
    if(wd & WPAD_BUTTON_1 || down & PAD_TRIGGER_L || userInput[0].cpad.data.down & CTR_BUTTON_L) {
        osdLevel ^= 1;
    } else if(!inDVDMenu && (wd & WPAD_BUTTON_2 || down & PAD_TRIGGER_R ||
            userInput[0].cpad.data.down & CTR_BUTTON_R)) {
        ToggleVideoSettings();
    } else if(AutobootExit && (home || back)) {
        VIDEO_SetBlack (TRUE);
        ExitRequested = true;
    } else if(ExitRequested || home || back) {
        if(wiiIsPaused())
            wiiPause();
        wiiGotoGui();
    }

    if(VideoSettingsCursor())
    {
        // the player settings were opened by a button: the d-pad is theirs
        // one row for each press, and more only once Up or Down has been
        // held a while, as a remote does
        static int wayBefore = 0;
        static u64 holdStart = 0, lastStep = 0;
        int way = wh & WPAD_BUTTON_UP || held & PAD_BUTTON_UP || userInput[0].cpad.data.held & CTR_BUTTON_UP ? -1 :
            wh & WPAD_BUTTON_DOWN || held & PAD_BUTTON_DOWN || userInput[0].cpad.data.held & CTR_BUTTON_DOWN ? 1 : 0;
        u64 now = gettime();
        bool step = false;
        if(way != wayBefore)
        {
            step = way != 0;
            holdStart = lastStep = now;
        }
        else if(way && diff_usec(holdStart, now) > SCROLL_DELAY_INITIAL && diff_usec(lastStep, now) > SCROLL_DELAY_LOOP)
        {
            step = true;
            lastStep = now;
        }
        wayBefore = way;

        if(step)
            VideoSettingsMove(way);
        else if(way)
            ; // held, waiting
        else if(wd & WPAD_BUTTON_LEFT || down & PAD_BUTTON_LEFT || userInput[0].cpad.data.down & CTR_BUTTON_LEFT)
            VideoSettingsBack();
        else if(wd & WPAD_BUTTON_RIGHT || down & PAD_BUTTON_RIGHT || userInput[0].cpad.data.down & CTR_BUTTON_RIGHT)
            VideoSettingsSelect();
        else if(wd & WPAD_BUTTON_A || down & PAD_BUTTON_A || userInput[0].cpad.data.down & CTR_BUTTON_A)
        {
            // with the pointer on the screen, A is a click: on a row, or
            // anywhere else to shut the panel
            if(!userInput[0].wpad->ir.valid)
                VideoSettingsSelect();
            else if(!VideoSettingsHit(userInput[0].wpad->ir.x, userInput[0].wpad->ir.y))
                HideVideoSettings();
        }
    }
    else if(!inDVDMenu)
    {
        if(wd & WPAD_BUTTON_A || down & PAD_BUTTON_A || userInput[0].cpad.data.down & CTR_BUTTON_A)
        {
            // Hack to allow people to unpause while the OSD GUI is visible by
            // pointing above the button bar and pressing A. We also need to be outside
            // the boundaries of the volume bar area, when it is visible
            int x = userInput[0].wpad->ir.x;
            int y = userInput[0].wpad->ir.y;

            int xoffset = 20;

            if(screenwidth > 640)
                xoffset = 90;

            // with the player settings open, A above the bar closes them,
            // and A on them is theirs
            if(drawGui && VideoSettingsVisible())
            {
                if(y < 360 && !VideoSettingsHit(x, y))
                    HideVideoSettings();
            }
            else if(!drawGui || (y < 360 &&
                (!VideoVolumeLevelBarVisible() || !(x > xoffset && x < xoffset+100 && y > 180))))
            {
                wiiPause();
            }
        }
        else if(wh & WPAD_BUTTON_PLUS || held & PAD_BUTTON_Y ||
            userInput[0].cpad.data.held & CTR_BUTTON_Y)
        {
            volnow = gettime();

            if(diff_usec(volprev, volnow) > VOL_DELAY)
            {
                volprev = volnow;
                WiiSettings.volume++;
                if(WiiSettings.volume > 100) WiiSettings.volume = 100;
                wiiSetVolume(WiiSettings.volume);
                volumeUpdated = true;
                ShowVideoVolumeLevelBar();
            }
        }
        else if(wh & WPAD_BUTTON_MINUS || held & PAD_BUTTON_X ||
            userInput[0].cpad.data.held & CTR_BUTTON_X)
        {
            volnow = gettime();

            if(diff_usec(volprev, volnow) > VOL_DELAY)
            {
                volprev = volnow;
                WiiSettings.volume--;
                if(WiiSettings.volume < 0) WiiSettings.volume = 0;
                wiiSetVolume(WiiSettings.volume);
                volumeUpdated = true;
                ShowVideoVolumeLevelBar();
            }
        }
        else if (wh & WPAD_BUTTON_B || held & PAD_TRIGGER_Z)
        {
            unsigned int delay = (resizeinitial == 1) ? RESIZE_INITIAL_DELAY : RESIZE_DELAY;
            int resizenow = gettime();

            if(wh & WPAD_BUTTON_RIGHT || held & PAD_BUTTON_RIGHT)
            {
                if(diff_usec(resizeprev, resizenow) > delay)
                {
                    resizeinitial++;
                    resizeprev = resizenow;
                    MPlayerResize(+0.003F, 0.00F);
                }
            }
            else if(wh & WPAD_BUTTON_LEFT || held & PAD_BUTTON_LEFT)
            {
                if(diff_usec(resizeprev, resizenow) > delay)
                {
                    resizeinitial++;
                    resizeprev = resizenow;
                    MPlayerResize(-0.003F, 0.00F);
                }
            }
            else if(wh & WPAD_BUTTON_UP || held & PAD_BUTTON_UP)
            {
                if(diff_usec(resizeprev, resizenow) > delay)
                {
                    resizeinitial++;
                    resizeprev = resizenow;
                    MPlayerResize(0.00F, +0.003F);
                }
            }
            else if(wh & WPAD_BUTTON_DOWN || held & PAD_BUTTON_DOWN)
            {
                if(diff_usec(resizeprev, resizenow) > delay)
                {
                    resizeinitial++;
                    resizeprev = resizenow;
                    MPlayerResize(0.00F, -0.003F);
                }
            }

            if(wd & (WPAD_BUTTON_RIGHT | WPAD_BUTTON_LEFT |
                                            WPAD_BUTTON_UP | WPAD_BUTTON_DOWN))
            {
                resizeinitial = 0;
            }
        }
        else if(wd & WPAD_BUTTON_RIGHT || down & PAD_BUTTON_RIGHT ||
        userInput[0].cpad.data.down & CTR_BUTTON_RIGHT)
        {
            wiiFastForward();
        }
        else if(wd & WPAD_BUTTON_LEFT || down & PAD_BUTTON_LEFT ||
            userInput[0].cpad.data.down & CTR_BUTTON_LEFT)
        {
            wiiRewind();
        }
        else if(wd & WPAD_BUTTON_UP || down & PAD_BUTTON_UP ||
            userInput[0].cpad.data.down & CTR_BUTTON_UP)
        {
            if(!wiiIsPaused())
                wiiSetProperty(MP_CMD_SUB_SELECT, 0);
        }
        else if(wd & WPAD_BUTTON_DOWN || down & PAD_BUTTON_DOWN ||
            userInput[0].cpad.data.down & CTR_BUTTON_DOWN)
        {
            if(!wiiIsPaused()) {
                wiiSetProperty(MP_CMD_SWITCH_AUDIO, 0);
                /* When changing audio tracks there is a small de-sync */
                /* So we seek to re-sync */
                wiiSync();
            }
        }
#if 0
        else if(down & PAD_TRIGGER_R)
        {
            // DVD Angle support, removed because of no practical use.
        //  if(!wiiIsPaused()) {
        //      wiiSetProperty(MP_CMD_SWITCH_ANGLE, 0);
        //  }

            timerRumbleEnable ^= 1;
            if(rumbleRandEnable != 0)
                timerRumbleEnable = false;
        }
        else if(down & PAD_TRIGGER_L)
        {
            rumbleRandEnable ^= 1;
            if(timerRumbleEnable != 0)
                rumbleRandEnable = false;
        }
#endif
    }
    else
    {
        if(userInput[0].wpad->ir.valid)
            wiiUpdatePointer((int)userInput[0].wpad->ir.x, (int)userInput[0].wpad->ir.y);

        if(wd & WPAD_BUTTON_A || down & PAD_BUTTON_A)
        {
            if(userInput[0].wpad->ir.valid)
                wiiDVDNav(MP_CMD_DVDNAV_MOUSECLICK);
            else
                wiiDVDNav(MP_CMD_DVDNAV_SELECT);
        }
        else if (wh & WPAD_BUTTON_B || held & PAD_TRIGGER_Z)
        {
            unsigned int delay = (resizeinitial == 1) ? RESIZE_INITIAL_DELAY : RESIZE_DELAY;
            int resizenow = gettime();

            if(wh & WPAD_BUTTON_RIGHT || held & PAD_BUTTON_RIGHT)
            {
                if(diff_usec(resizeprev, resizenow) > delay)
                {
                    resizeinitial++;
                    resizeprev = resizenow;
                    MPlayerResize(+0.003F, 0.00F);
                }
            }
            else if(wh & WPAD_BUTTON_LEFT || held & PAD_BUTTON_LEFT)
            {
                if(diff_usec(resizeprev, resizenow) > delay)
                {
                    resizeinitial++;
                    resizeprev = resizenow;
                    MPlayerResize(-0.003F, 0.00F);
                }
            }
            else if(wh & WPAD_BUTTON_UP || held & PAD_BUTTON_UP)
            {
                if(diff_usec(resizeprev, resizenow) > delay)
                {
                    resizeinitial++;
                    resizeprev = resizenow;
                    MPlayerResize(0.00F, +0.003F);
                }
            }
            else if(wh & WPAD_BUTTON_DOWN || held & PAD_BUTTON_DOWN)
            {
                if(diff_usec(resizeprev, resizenow) > delay)
                {
                    resizeinitial++;
                    resizeprev = resizenow;
                    MPlayerResize(0.00F, -0.003F);
                }
            }

            if(wd & (WPAD_BUTTON_RIGHT | WPAD_BUTTON_LEFT |
                                            WPAD_BUTTON_UP | WPAD_BUTTON_DOWN))
            {
                resizeinitial = 0;
            }
        }
        else if(wd & WPAD_BUTTON_UP || down & PAD_BUTTON_UP)
        {
            wiiDVDNav(MP_CMD_DVDNAV_UP);
        }
        else if(wd & WPAD_BUTTON_DOWN || down & PAD_BUTTON_DOWN)
        {
            wiiDVDNav(MP_CMD_DVDNAV_DOWN);
        }
        else if(wd & WPAD_BUTTON_RIGHT || down & PAD_BUTTON_RIGHT)
        {
            wiiDVDNav(MP_CMD_DVDNAV_RIGHT);
        }
        else if(wd & WPAD_BUTTON_LEFT || down & PAD_BUTTON_LEFT)
        {
            wiiDVDNav(MP_CMD_DVDNAV_LEFT);
        }
    }

    if(volumeUpdated)
    {
        volnow = gettime();

        if(volnow > volprev && diff_usec(volprev, volnow) > VOLDISP_MAX)
            volumeUpdated = false;
        else
            ir = true; // trigger display
    }

    if(ir || BufferingStatusSet() || osdLevel || VideoSettingsCursor())
    {
        drawGui = true;
    }
    else if(drawGui)
    {
        drawGui = false;
        HideVideoVolumeLevelBar();
        HideVideoSettings();
        ShutoffRumble();

    //  rumbleRandEnable = false;
    //  timerRumbleEnable = false;

        PAD_ControlMotor(0, PAD_MOTOR_STOP);
    }

    //rumble in video
#if 0
    if(rumbleRandEnable) {
        if(timerRumble == 0)
            timerRumble = rand() % (70 + 1 - 1) + 1;

        --timerRumble;
        if(timerRumble < 8) {
            PAD_ControlMotor(0, PAD_MOTOR_RUMBLE);
            PAD_ControlMotor(1, PAD_MOTOR_RUMBLE);
        } else {
            PAD_ControlMotor(0, PAD_MOTOR_STOP);
            PAD_ControlMotor(1, PAD_MOTOR_STOP);
        }
    }
    else if(timerRumbleEnable) {
        if(timerRumble == 0)
            timerRumble = 12;

        --timerRumble;
        if(timerRumble < 4) {
            PAD_ControlMotor(0, PAD_MOTOR_RUMBLE);
            PAD_ControlMotor(1, PAD_MOTOR_RUMBLE);
        } else {
            PAD_ControlMotor(0, PAD_MOTOR_STOP);
            PAD_ControlMotor(1, PAD_MOTOR_STOP);
        }
    }
#endif
}
