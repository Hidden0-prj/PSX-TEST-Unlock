/*
 * ps1-ram-tester - UI sound effects
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Uploads the UI sound effect to SPU RAM. Must be called once after
// initSPU().
void initSound(void);

// Plays the sound effect on the "scroll" channel (menu cursor movement).
void playScrollSound(void);

// Plays the same sound effect on a separate "confirm" channel (selecting
// a menu item), so a rapid scroll-then-confirm doesn't cut either sound
// off early.
void playConfirmSound(void);

// Starts the looping background music. Call once at startup, after
// initSound(). The SPU loops it in hardware from then on - no per-frame
// code needed anywhere else.
void playBGM(void);

// Plays the dedicated SPU test tone on an arbitrary channel (0-23). Used
// by the SPU channel test to check each channel individually. Note that
// channels 0-2 are normally used for scroll/confirm/BGM - testing those
// channels will interrupt whatever they were doing, so the caller is
// responsible for restoring normal playback afterward (see
// initSound()/playBGM()).
void playTestTone(int channel);

// Toggles background music on/off (mutes/restores its volume - doesn't
// stop the SPU from looping it, just silences it). Returns the new state.
bool toggleBGM(void);
bool isBGMEnabled(void);

// Toggles scroll/confirm UI sound effects on/off. Doesn't affect BGM or
// playTestTone(), only playScrollSound()/playConfirmSound(). Returns the
// new state.
bool toggleSFX(void);
bool isSFXEnabled(void);

#ifdef __cplusplus
}
#endif
