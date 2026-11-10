/****************************************************************************
 * WiiMC
 * Tantric 2009-2012
 *
 * input.h
 * Wii/GameCube controller management
 ***************************************************************************/

#ifndef _INPUT_H_
#define _INPUT_H_

#include <gccore.h>
#include <wiiuse/wpad.h>
#include <wiikeyboard/keyboard.h>
#include "utils/3ds.h"

#define PI              3.14159265f
#define PADCAL          30

void SetupPads();
void UpdatePads(bool playing = false);
void KeyboardTyping(bool on);
bool KeyboardGetKey(keyboard_event *ev);
void DisableRumble();
void EnableRumble();
void ShutoffRumble();
void RequestRumble(int i);
void DoRumble(int i);
void MPlayerInput();

#endif
