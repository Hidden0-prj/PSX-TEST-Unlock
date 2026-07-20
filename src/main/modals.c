/*
 * ps1-ram-tester - (C) 2026 spicyjpeg
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdint.h>
#include "common/reboot.h"
#include "main/cdboot.h"
#include "main/defs.h"
#include "main/mainmenu.h"
#include "main/modals.h"
#include "main/ramtester.h"
#include "main/renderer.h"
#include "main/ui.h"
#include "ps1/registers.h"

/* Reboot functions */

static void showRebootProgress(RenderContext *ctx, const char *message) {
	// This needs to be done multiple times in order to completely "flush" the
	// rendering pipeline.
	for (int i = 0; i < 3; i++) {
		beginFrame(ctx);
		renderProgressScreen(ctx, 0, 1, message);
		endFrame(ctx);
	}
}

/* ---- CD-ROM CD-R/import unlock sequence ----
 *
 * This is genuine, well-established homebrew dev tooling, not a piracy
 * mechanism specifically - CD-Rs don't have the physical SCEx wobble
 * signal a pressed disc has, so testing your own burned homebrew discs
 * on real hardware without a modchip needs exactly this. Same category
 * of tool as n00brom (Lameguy64's open-source PS1 dev cartridge
 * firmware) and UniROM, which document/use this same mechanism.
 *
 * The sequence is the SecretUnlock commands documented by psx-spx:
 * standalone command bytes 0x50..0x56, each carrying its portion of the
 * "Licensed by Sony Computer Entertainment <region>" string as command
 * PARAMETERS (NOT as sub-functions of the 0x19 Test command). Every one
 * of these commands answers with INT5(0x11,0x40) even on success, and -
 * importantly - the controller does NOT drain the parameter FIFO when it
 * returns that INT5, so the FIFO must be cleared (CLRPRM) after each
 * command or the leftover parameters corrupt the next command.
 *
 * These commands exist only on BIOS/controller version 0xC1 and up, and
 * do nothing on Japanese consoles (region text "America" is used here,
 * matching every board this project has been tested against).
 *
 * Uses the same BUSYSTS-synchronized command infrastructure as the
 * CD-ROM controller test and CD player.
 */

#define CDROM_REG0     (*(volatile uint8_t  *) 0xbf801800)
#define CDROM_REG1     (*(volatile uint8_t  *) 0xbf801801)
#define CDROM_REG2     (*(volatile uint8_t  *) 0xbf801802)
#define CDROM_REG3     (*(volatile uint8_t  *) 0xbf801803)
#define CDROM_COMDELAY (*(volatile uint32_t *) 0xbf801020)

// Per-command pre-step (UniROM sub_E1EF0): wait for the controller to go
// idle (DRQSTS then BUSYSTS clear), then acknowledge/clear all pending
// IRQ flags. Leaves bank 0 selected.
static void cdromPreStep(void) {
	while (CDROM_REG0 & 0x40) // DRQSTS
		;
	while (CDROM_REG0 & 0x80) // BUSYSTS
		;

	CDROM_REG0 = 1;    // bank 1
	CDROM_REG3 = 0x1f; // HCLRCTL: acknowledge all IRQ flags
	CDROM_REG0 = 0;    // bank 0
}

static int cdromWaitIRQ(void) {
	uint32_t timeout = 2000000;
	uint8_t  flags;

	CDROM_REG0 = 1; // bank 1 (HINTSTS)
	do {
		flags = CDROM_REG3 & 0x07;
		if (--timeout == 0)
			return -1;
	} while (flags == 0);

	return flags;
}

// Send command byte + optional parameter string, wait for the first
// response, then acknowledge. Matches UniROM's per-command flow.
static void cdromCommand(uint8_t cmd, const char *paramStr) {
	cdromPreStep();

	if (paramStr)
		for (const char *p = paramStr; *p; p++)
			CDROM_REG2 = (uint8_t) *p; // PARAMETER FIFO (bank 0)

	CDROM_REG1 = cmd; // COMMAND (bank 0)

	cdromWaitIRQ();

	CDROM_REG0 = 1;    // bank 1
	CDROM_REG3 = 0x1f; // acknowledge all IRQ flags
}

// Full UniROM-accurate CD init + SecretUnlock, reconstructed from a
// disassembly of UniROM's own unlock routine. The essential parts the
// previous version was missing: setting Com_Delay (0x1F801020 = 0x1325)
// and running Nop/Nop/Init(0x0A) to put the drive in a known state BEFORE
// the 0x50..0x56 SecretUnlock commands. Without that init the commands
// are accepted (INT5) but the drive never actually unlocks. Region string
// for 0x55 is "of America" (US console).
static void sendCDROMUnlockSequence(void) {
	// CD init (psx-spx "To init the CD" / UniROM's pre-unlock init).
	CDROM_REG0 = 1;    CDROM_REG3 = 0x1f; // ack all IRQs
	CDROM_REG0 = 0;    CDROM_REG3 = 0x00; // HCHPCTL = 0
	CDROM_COMDELAY = 0x1325;              // Com_Delay = 4901

	cdromCommand(0x01, NULL); // Nop
	cdromCommand(0x01, NULL); // Nop
	cdromCommand(0x0a, NULL); // Init (INT3)
	cdromWaitIRQ();                        // Init 2nd response (INT2)
	CDROM_REG0 = 1; CDROM_REG3 = 0x1f;

	// SecretUnlock 0x50..0x56.
	cdromCommand(0x50, NULL);
	cdromCommand(0x51, "Licensed by");
	cdromCommand(0x52, "Sony");
	cdromCommand(0x53, "Computer");
	cdromCommand(0x54, "Entertainment");
	cdromCommand(0x55, "of America");
	cdromCommand(0x56, NULL);
}

static void doFastReboot(
	RenderContext  *ctx,
	UIState        *state,
	const MenuItem *item
) {
	(void) state;
	(void) item;

	showRebootProgress(ctx, "Waiting for kernel to load CD-ROM...");
	// vramSize is set by the RAM/VRAM/SPU RAM tester submenu, defaulting to
	// 0 (standard 1 MB VRAM) if that submenu was never entered.
	softFastRebootWithConfig(DRAM_CTRL & 0xffff, vramSize);
	__builtin_unreachable();
}

// Same as doFastReboot(), but first sends the CD-ROM unlock sequence so a
// CD-R or import disc without a modchip can boot. This has NOT been
// verified against a real, complete reference implementation - built
// carefully from psx-spx's own documented command table, but expect this
// to need real-hardware testing/iteration, same as the CD-ROM controller
// test did before it was confirmed working.
static void doFastRebootUnlocked(
	RenderContext  *ctx,
	UIState        *state,
	const MenuItem *item
) {
	(void) state;
	(void) item;

	showRebootProgress(ctx, "Sending CD-R/import unlock sequence...");
	sendCDROMUnlockSequence();

	showRebootProgress(ctx, "Waiting for kernel to load CD-ROM...");
	softFastRebootWithConfig(DRAM_CTRL & 0xffff, vramSize);
	__builtin_unreachable();
}

// Boot the CD the way UniROM does (confirmed by disassembly): send the
// CD-R/import unlock sequence (harmless on a licensed pressed disc,
// required for a burned CD-R), then perform a clean jump to the BIOS
// reset vector at 0xBFC00000 via softReset(). The BIOS then re-reads
// SYSTEM.CNF and boots the disc itself - a genuine cold boot with NO
// kernel RAM-config patch.
//
// Trade-offs vs. the "Boot CD-ROM" fast-reboot option above:
//  + Works on ANY BIOS (doesn't need the fast-reboot kernel patch), so
//    it's offered even on the "incompatible BIOS" error screen.
//  + Maximum game compatibility (uses the exact path a normal power-on
//    boot uses).
//  - The boot logo plays (a few seconds), since it's a real cold boot.
//  - Does not apply the RAM/VRAM test config to the booted game.
static void doBootCDUnirom(
	RenderContext  *ctx,
	UIState        *state,
	const MenuItem *item
) {
	(void) state;
	(void) item;

	showRebootProgress(ctx, "Sending CD-R/import unlock sequence...");
	sendCDROMUnlockSequence();

	showRebootProgress(ctx, "Booting CD via BIOS (UniROM-style)...");
	softReset();
	__builtin_unreachable();
}

void doFullReboot(RenderContext *ctx, UIState *state, const MenuItem *item) {
	(void) state;
	(void) item;

	showRebootProgress(ctx, "Waiting for kernel to reboot...");
	softReset();
	__builtin_unreachable();
}

/* Fast reboot warning and error menus */

static const MenuItem rebootWarningMenu[] = {
	{
		.name = "Warning",
		.type = ITEM_TITLE
	}, {
		.type = ITEM_SEPARATOR
	}, {
		.name = "CD-ROM booting patches the kernel to apply RAM config -",
		.type = ITEM_STATIC
	}, {
		.name = "this can cause compatibility issues with some games.",
		.type = ITEM_STATIC
	}, {
		.type = ITEM_SEPARATOR
	}, {
		.name = "If you understand the risks, insert a disc and close",
		.type = ITEM_STATIC
	}, {
		.name = "the lid before proceeding.",
		.type = ITEM_STATIC
	}, {
		.type = ITEM_SEPARATOR
	}, {
		.name   = "Boot CD-ROM",
		.type   = ITEM_ACTION,
		.action = { .callback = doFastReboot }
	}, {
		.name   = "Boot CD-R (Unlock)",
		.type   = ITEM_ACTION,
		.action = { .callback = bootCDR }
	}, {
		.name   = "Boot CD (UNIROM)",
		.type   = ITEM_ACTION,
		.action = { .callback = doBootCDUnirom }
	}, {
		.name   = "Cancel",
		.type   = ITEM_ACTION,
		.action = { .callback = enterMainMenu }
	}, {
		.type = ITEM_END
	}
};

static const MenuItem rebootErrorMenu[] = {
	{
		.name = "Error",
		.type = ITEM_TITLE
	}, {
		.type = ITEM_SEPARATOR
	}, {
		.name = "This console's BIOS ROM version is not supported or not",
		.type = ITEM_STATIC
	}, {
		.name = "compatible with the kernel patch used for CD-ROM booting.",
		.type = ITEM_STATIC
	}, {
		.name = "Only Sony's own retail BIOS ROMs are currently supported.",
		.type = ITEM_STATIC
	}, {
		.type = ITEM_SEPARATOR
	}, {
		.name = "The UniROM-style boot below does not use that patch and",
		.type = ITEM_STATIC
	}, {
		.name = "may still work - insert a disc, close the lid, and try it.",
		.type = ITEM_STATIC
	}, {
		.type = ITEM_SEPARATOR
	}, {
		.name   = "Boot CD (UNIROM)",
		.type   = ITEM_ACTION,
		.action = { .callback = doBootCDUnirom }
	}, {
		.name   = "Back",
		.type   = ITEM_ACTION,
		.action = { .callback = enterMainMenu }
	}, {
		.type = ITEM_END
	}
};

void enterFastRebootMenu(
	RenderContext  *ctx,
	UIState        *state,
	const MenuItem *item
) {
	(void) ctx;
	(void) item;

	if (isFastRebootCompatible()) {
		state->currentMenu = rebootWarningMenu;
		state->menuCursor  = (sizeof(rebootWarningMenu) / sizeof(MenuItem)) - 2;
	} else {
		state->currentMenu = rebootErrorMenu;
		state->menuCursor  = (sizeof(rebootErrorMenu)   / sizeof(MenuItem)) - 2;
	}
}

/* "About ps1-ram-tester" menu */

static const MenuItem aboutMenu[] = {
	{
		.name = "- PSX-iTests v1.0 -",
		.type = ITEM_TITLE
	}, {
		.type = ITEM_SEPARATOR
	}, {
		.name = "Version " VERSION_STRING,
		.type = ITEM_STATIC
	}, {
		.name = "PS1 hardware diagnostic and testing tool.",
		.type = ITEM_STATIC
	}, {
		.type = ITEM_SEPARATOR
	}, {
		.name = "Built on ps1-bare-metal and ps1-ram-tester",
		.type = ITEM_STATIC
	}, {
		.name = "by spicyjpeg (MIT license).",
		.type = ITEM_STATIC
	}, {
		.type = ITEM_SEPARATOR
	}, {
		.name = "Free and open source.",
		.type = ITEM_STATIC
	}, {
		.type = ITEM_SEPARATOR
	}, {
		.name = "Developed with AI assistance (Claude).",
		.type = ITEM_STATIC
	}, {
		.type = ITEM_SEPARATOR
	}, {
		.name   = "Back",
		.type   = ITEM_ACTION,
		.action = { .callback = enterMainMenu }
	}, {
		.type = ITEM_END
	}
};

void enterAboutMenu(RenderContext *ctx, UIState *state, const MenuItem *item) {
	(void) ctx;
	(void) item;

	state->currentMenu = aboutMenu;
	state->menuCursor  = (sizeof(aboutMenu) / sizeof(MenuItem)) - 2;
}
