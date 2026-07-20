/*
 * PSX-iTests - RAM/VRAM/SPU RAM tester submenu
 *
 * Ported from spicyjpeg's ps1-ram-tester (https://github.com/spicyjpeg/ps1-ram-tester),
 * used here under the MIT license. The test logic (test.c) and RAM config
 * submenu (ramconfig.c) are carried over essentially unchanged; this file
 * is adapted from the original mainmenu.c, with the entry point renamed
 * from enterMainMenu() to enterRAMTesterMenu() (since that name is now
 * taken by PSX-iTests' own top-level menu) and the reboot/about menu items
 * removed, since PSX-iTests already has its own copies of those at the top
 * level - a "Back to main menu" item replaces them here instead.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "common/spu.h"
#include "main/mainmenu.h"
#include "main/ramconfig.h"
#include "main/ramtester.h"
#include "main/renderer.h"
#include "main/sound.h"
#include "main/test.h"
#include "main/ui.h"
#include "ps1/gpucmd.h"
#include "ps1/registers.h"

/* Menu callbacks */

uint8_t vramSize   =  0;
uint8_t testPasses = 10;

static char mainRAMConfig[32] = "";
static char finalResult[32]   = "";
static char mainRAMResult[32] = "";
static char vramResult[32]    = "";
static char spuRAMResult[32]  = "";

static const char *testMessage = 0;

extern char _bssEnd[];

static void testCallback(void *arg, int progress, int total) {
	RenderContext *ctx = (RenderContext *) arg;
	char          message[64];

	snprintf(message, sizeof(message), testMessage, progress / 2 + 1);

	beginFrame(ctx);
	renderProgressScreen(ctx, progress, total, message);
	endFrame(ctx);
}

static void runMainRAMTest(
	RenderContext  *ctx,
	UIState        *state,
	const MenuItem *item
) {
	testMessage = "Running main RAM test... (pass %d)";

	size_t    size = getMainRAMSize();
	TestError error;

	if (testMainRAM(
		&error,
		testCallback,
		ctx,
		(uintptr_t) _bssEnd,
		RAM_BASE | size,
		testPasses
	))
		snprintf(
			mainRAMResult,
			sizeof(mainRAMResult),
			"Passed (%d MB)",
			size / 0x100000
		);
	else
		snprintf(
			mainRAMResult,
			sizeof(mainRAMResult),
			"Error at 0x%08X (pass %d)",
			error.address,
			error.pass + 1
		);

	enterRAMTesterMenu(ctx, state, item);
}

static void runVRAMTest(
	RenderContext  *ctx,
	UIState        *state,
	const MenuItem *item
) {
	testMessage = "Running VRAM test... (pass %d)";

	GPU_GP1 = gp1_vramSize(vramSize);

	TestError error;

	if (testVRAM(
		&error,
		0,
		0,
		0,
		512 << vramSize,
		testPasses
	))
		snprintf(
			vramResult,
			sizeof(vramResult),
			"Passed (%d MB)",
			1 << vramSize
		);
	else
		snprintf(
			vramResult,
			sizeof(vramResult),
			"Error at 0x%06X (pass %d)",
			error.address,
			error.pass + 1
		);

	reloadTextures(ctx);
	enterRAMTesterMenu(ctx, state, item);
}

static void runSPURAMTest(
	RenderContext  *ctx,
	UIState        *state,
	const MenuItem *item
) {
	testMessage = "Running SPU RAM test... (pass %d)";

	TestError error;

	if (testSPURAM(
		&error,
		testCallback,
		ctx,
		SPU_RAM_ALLOC_OFFSET,
		SPU_RAM_SIZE,
		testPasses
	))
		snprintf(
			spuRAMResult,
			sizeof(spuRAMResult),
			"Passed (%d KB)",
			SPU_RAM_SIZE / 1024
		);
	else
		snprintf(
			spuRAMResult,
			sizeof(spuRAMResult),
			"Error at 0x%05X (pass %d)",
			error.address,
			error.pass + 1
		);

	// The test above overwrites all of SPU RAM (including the UI sounds and
	// BGM data that live there) with test patterns, so both need to be
	// re-uploaded and the BGM restarted from scratch - simply re-keying the
	// channel isn't enough, since the actual sample bytes are gone.
	initSound();
	playBGM();

	enterRAMTesterMenu(ctx, state, item);
}

static void runAllTests(
	RenderContext  *ctx,
	UIState        *state,
	const MenuItem *item
) {
	runMainRAMTest(ctx, state, item);
	runVRAMTest(ctx, state, item);
	runSPURAMTest(ctx, state, item);
}

/* RAM tester submenu */

static const MenuItem ramTesterMenu[] = {
	{
		.name = "- RAM/VRAM/SPU RAM Tester -",
		.type = ITEM_TITLE
	}, {
		.type = ITEM_SEPARATOR
	}, {
		.name   = "Configure main RAM...",
		.type   = ITEM_ACTION,
		.action = {
			.tag      = mainRAMConfig,
			.callback = enterRAMConfigMenu
		}
	}, {
		.name     = "VRAM size",
		.type     = ITEM_ENUM,
		.minValue = 0,
		.maxValue = 1,
		.enum_    = {
			.value = &vramSize,
			.items = (const char *const[]) {
				"1 MB (retail/dev)",
				"2 MB (arcade)"
			}
		}
	}, {
		.type = ITEM_SEPARATOR
	}, {
		.name     = "Number of test passes",
		.type     = ITEM_INT,
		.minValue =   1,
		.maxValue = 250,
		.int_     = { .value = &testPasses }
	}, {
		.type = ITEM_SEPARATOR
	}, {
		.name   = "Test main RAM, VRAM and SPU RAM",
		.type   = ITEM_ACTION,
		.action = {
			.tag      = finalResult,
			.callback = runAllTests
		}
	}, {
		.name     = "Test main RAM",
		.type     = ITEM_ACTION,
		.action = {
			.tag      = mainRAMResult,
			.callback = runMainRAMTest
		}
	}, {
		.name     = "Test VRAM",
		.type     = ITEM_ACTION,
		.action = {
			.tag      = vramResult,
			.callback = runVRAMTest
		}
	}, {
		.name     = "Test SPU RAM",
		.type     = ITEM_ACTION,
		.action = {
			.tag      = spuRAMResult,
			.callback = runSPURAMTest
		}
	}, {
		.type = ITEM_SEPARATOR
	}, {
		.name = "Warning: the screen will flicker while testing VRAM.",
		.type = ITEM_STATIC
	}, {
		.type = ITEM_SEPARATOR
	}, {
		.name   = "Back to main menu",
		.type   = ITEM_ACTION,
		.action = { .callback = enterMainMenu }
	}, {
		.type = ITEM_END
	}
};

void enterRAMTesterMenu(RenderContext *ctx, UIState *state, const MenuItem *item) {
	(void) ctx;
	(void) item;

	// Moved here from main()'s global startup sequence - this submenu
	// specifically needs the "accidental 8MB retail BIOS" correction
	// (see fixRetailRAMConfig()'s own comment) since it actually tests
	// memory for faults and would otherwise try to test 8MB of
	// physically nonexistent RAM on a genuinely-2MB console. Applying it
	// globally at boot, unconditionally, was masking real RAM
	// configuration (including genuine 8MB/16MB setups) everywhere else
	// in the app before the user ever saw any screen - a real bug, not
	// a testing error, confirmed by DuckStation's 8MB setting reading as
	// 2MB no matter what.
	fixRetailRAMConfig();

	snprintf(
		mainRAMConfig,
		sizeof(mainRAMConfig),
		"%d MB [0x%04X]",
		getMainRAMSize() / 0x100000,
		DRAM_CTRL & 0xffff
	);

	// Somewhat ugly hack, but it works.
	if (
		(mainRAMResult[0] == 'E') ||
		(vramResult[0]    == 'E') ||
		(spuRAMResult[0]  == 'E')
	)
		__builtin_strncpy(finalResult, "Failed", sizeof(finalResult));
	else if (
		(mainRAMResult[0] == 'P') &&
		(vramResult[0]    == 'P') &&
		(spuRAMResult[0]  == 'P')
	)
		__builtin_strncpy(finalResult, "Passed", sizeof(finalResult));
	else
		finalResult[0] = 0;

	state->currentMenu = ramTesterMenu;
	state->menuCursor  = 0;
}
