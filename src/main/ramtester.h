/*
 * PSX-iTests - RAM/VRAM/SPU RAM tester submenu
 *
 * Ported from spicyjpeg's ps1-ram-tester, adapted to run as a submenu of
 * PSX-iTests rather than as the top-level menu. Renamed from the original
 * enterMainMenu() to enterRAMTesterMenu() to avoid colliding with our own
 * top-level main/mainmenu.h, which already defines enterMainMenu() for the
 * PSX-iTests root menu.
 */

#pragma once

#include "main/renderer.h"
#include "main/ui.h"

#ifdef __cplusplus
extern "C" {
#endif

extern uint8_t vramSize;
extern uint8_t testPasses;

void enterRAMTesterMenu(
	RenderContext  *ctx,
	UIState        *state,
	const MenuItem *item
);

#ifdef __cplusplus
}
#endif
