/*
 * PSX-iTests - Console information (BIOS/motherboard/GPU detection)
 */

#pragma once

#include "main/renderer.h"
#include "main/ui.h"

#ifdef __cplusplus
extern "C" {
#endif

// Menu callback: reads and displays BIOS date/version/region info.
// Temporarily stripped down to only this (see console_info.c for why) -
// CD-ROM/GPU/RAM/CPU detection will be added back incrementally once this
// is confirmed stable on real hardware. Pure read-only memory access,
// nothing is modified. Exits on any button press.
void runConsoleInfo(
	RenderContext  *ctx,
	UIState        *state,
	const MenuItem *item
);

// Diagnostic: raw hex+ASCII dump of the BIOS string region (0xBFC00100
// onward), used to see exactly what's actually there on real hardware,
// rather than guessing at the string layout. Pure read-only memory
// access, same as runConsoleInfo(). Exits on any button press.
void runBiosMemoryDump(
	RenderContext  *ctx,
	UIState        *state,
	const MenuItem *item
);

// Diagnostic: CD-ROM controller (Mechacon) version test, rebuilt against
// tonyhax's real, production CD-ROM command sequence after our earlier
// version proved unreliable on real hardware. Shows the raw response
// bytes, the interrupt code (should be 3 for success), and the matched
// motherboard revision if recognized. Exits on any button press.
void runCDROMTest(
	RenderContext  *ctx,
	UIState        *state,
	const MenuItem *item
);

// Diagnostic: Main RAM size test (memory fold technique, 2/4/8/16 MB
// tiers). This is a different kind of test from ps1-ram-tester's own RAM
// test - that one hunts for bad memory cells and needs many passes; this
// one just answers "how much RAM is here" in a handful of writes/reads,
// no passes needed. Exits on any button press.
void runRAMTest(
	RenderContext  *ctx,
	UIState        *state,
	const MenuItem *item
);

// Diagnostic: standalone GPU chip generation test (160-pin vs 208-pin).
// Also shows which 16MB-unlock magic value applies to the detected GPU,
// for cross-referencing against the RAM test's unlock attempt. Exits on
// any button press.
void runGPUVersionTest(
	RenderContext  *ctx,
	UIState        *state,
	const MenuItem *item
);

// Diagnostic: runs the CD-R/import SecretUnlock sequence step by step and
// displays the interrupt code + response bytes for every command, plus
// GetID before/after, so unlock failures can be diagnosed on real
// hardware. Read-only (does not reboot). Exits on any button press.
void runCDUnlockDebug(
	RenderContext  *ctx,
	UIState        *state,
	const MenuItem *item
);

#ifdef __cplusplus
}
#endif
