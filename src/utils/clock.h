/****************************************************************************
 * WiiMC
 *
 * clock.h
 *
 * WiiMC's time of day, corrected over the internet when the Wii's clock is
 * wrong
 ***************************************************************************/

#ifndef _CLOCK_H_
#define _CLOCK_H_

// Asks a server on the internet for the time, and makes WiiMC's time of day
// follow it if the Wii's clock is wrong. Takes a network connection.
void ClockCorrectOnline();

#endif
