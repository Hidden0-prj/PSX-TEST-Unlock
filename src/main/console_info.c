/*
 * PSX-iTests - Console information (BIOS detection only, for now)
 *
 * Deliberately stripped down to JUST BIOS date/version/region detection.
 * The full version (BIOS + CD-ROM Mechacon + GPU + RAM size + CPU ID)
 * crashes on real hardware, even though it worked fine in DuckStation.
 * Since the very first version of this screen worked on real hardware,
 * something added afterward broke it - rather than keep guessing at
 * which piece, we're isolating them one at a time, starting with the
 * piece that's the safest by construction: BIOS detection is pure,
 * read-only memory access (KSEG1 uncached BIOS ROM space), no hardware
 * registers are written to at all, so it's the least likely piece to be
 * the actual cause - but confirming it works standalone on real hardware
 * rules it out for certain before moving on to the CD-ROM/GPU/RAM/CPU
 * checks, which all involve actual hardware register reads/writes and
 * are much more likely culprits.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "common/sio0.h"
#include "main/console_info.h"
#include "main/defs.h"
#include "main/font.h"
#include "main/mainmenu.h"
#include "main/ramconfig.h"
#include "ps1/registers.h"

/* ---- BIOS version/date/region ----
 *
 * CONFIRMED via psx-spx's BIOS Memory Map: BFC00100h holds a BCD-encoded
 * date (format YYYYMMDDh), and BFC00108h holds the "Kernel Maker/Version
 * Strings" (the maker name only, in practice - see getBiosVersionString()
 * below for the actual version string).
 *
 * CORRECTION: an earlier version of this code avoided the commonly-cited
 * fixed address 0xBFC7FF32 for the version string, based on The Cutting
 * Room Floor's PS1 disassembly notes claiming that offset moves between
 * BIOS revisions (SCPH-1001 at 0x42E74, SCPH-1002 at 0x423C0). That
 * turned out to be wrong: directly searching 4 real BIOS dumps (v2.2,
 * v3.0, v4.1, v4.5, spanning 1995-2000, including the exact SCPH-1001
 * BIOS TCRF was describing) found the real, actively displayed version
 * string at 0xBFC7FF32 in every single one. The "different offset" TCRF
 * found is real, but it's a generic leftover "System ROM Version 1.0"
 * template string sitting elsewhere in some BIOS builds' kernel code -
 * not the real one. Lesson: a documentation claim is still worth
 * checking directly against real files when possible, rather than taken
 * as final.
 */

static uint8_t bcdToDec(uint8_t bcd) {
	return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

static void getBiosDate(char *out, size_t outSize) {
	uint32_t value = *(const volatile uint32_t *) 0xbfc00100;

	uint8_t yearHi = (value >> 24) & 0xff;
	uint8_t yearLo = (value >> 16) & 0xff;
	uint8_t month  = (value >>  8) & 0xff;
	uint8_t day    =  value        & 0xff;

	int year = (int) bcdToDec(yearHi) * 100 + (int) bcdToDec(yearLo);

	snprintf(
		out, outSize, "%04d-%02d-%02d",
		year, bcdToDec(month), bcdToDec(day)
	);
}

static const char *getBiosMakerString(void) {
	return (const char *) 0xbfc00108;
}

// BFC00108h actually holds MULTIPLE null-terminated strings back to back
// (documented by psx-spx as "Kernel Maker/Version Strings", plural) - the
// maker name ("Sony Computer Entertainment Inc.") comes first, followed
// by the actual "System ROM Version X.X MM/DD/YY R" string. Scans forward
// through consecutive null-terminated strings looking for that prefix
// specifically, rather than assuming the first string is the right one.
static bool stringStartsWith(const char *str, const char *prefix) {
	while (*prefix) {
		if (*str != *prefix)
			return false;
		str++;
		prefix++;
	}
	return true;
}

static const char *getBiosVersionString(void) {
	// CONFIRMED directly against 4 real BIOS dumps (v2.2, v3.0, v4.1,
	// v4.5, spanning 1995-2000): the active, displayed version string is
	// reliably at this exact address in every one of them. An earlier
	// round of research suggested this address wasn't reliable and moved
	// between revisions - that turned out to be based on a false
	// positive (a generic leftover "System ROM Version 1.0" template
	// string that exists elsewhere in some BIOS builds' kernel code, not
	// the real, displayed one). This was verified by directly searching
	// real BIOS files rather than assumed from documentation.
	const char *direct = (const char *) 0xbfc7ff32;
	if (stringStartsWith(direct, "System ROM Version"))
		return direct;

	// Fallback: scan the whole BIOS ROM region for the string, in case
	// some exotic/unusual BIOS revision genuinely differs from every
	// version actually tested. 512KB scanned a byte at a time is still
	// well under a second even on the original CPU, so this is safe to
	// do live if it's ever actually needed - which, per the above, it
	// shouldn't be for any real Sony BIOS.
	const char *base = (const char *) 0xbfc00000;
	for (uint32_t offset = 0; offset < (0x80000 - 19); offset++) {
		if (stringStartsWith(base + offset, "System ROM Version"))
			return base + offset;
	}

	// Genuinely not found anywhere - fall back to the maker string so
	// there's still something reasonable to display.
	return getBiosMakerString();
}

// The region letter is the last non-whitespace character of the version
// string (e.g. "...12/04/95 A" -> 'A').
static char getBiosRegionLetter(void) {
	const char *str = getBiosVersionString();

	size_t len = 0;
	while (str[len])
		len++;

	while (len > 0) {
		char c = str[len - 1];
		if ((c != ' ') && (c != '\r') && (c != '\n'))
			break;
		len--;
	}

	return (len > 0) ? str[len - 1] : '?';
}

static const char *regionName(char letter) {
	switch (letter) {
		case 'A': return "Americas (NTSC)";
		case 'E': return "Europe (PAL)";
		case 'I': return "Japan (early NTSC-J)";
		case 'J': return "Japan (NTSC-J)";
		default:  return "Unknown/non-standard";
	}
}

/* ---- Display ---- */

void runConsoleInfo(
	RenderContext  *ctx,
	UIState        *state,
	const MenuItem *item
) {
	(void) state;
	(void) item;

	char biosDate[16];
	getBiosDate(biosDate, sizeof(biosDate));
	const char *biosVersion      = getBiosVersionString();
	char        biosRegionLetter = getBiosRegionLetter();

	char line[64];

	// Debounce: wait for the button that opened this screen to be released.
	while (pollController(0) | pollController(1))
		;

	for (;;) {
		if (pollController(0) | pollController(1))
			break;

		beginFrame(ctx);
		drawBackground(ctx);

		printString(ctx, 16, 14, 0x808080, "CONSOLE INFORMATION (BIOS ONLY - testing)");

		printString(ctx, 16, 32, 0x505050, "BIOS");
		snprintf(
			line, sizeof(line), "Date: %s   Region: %c (%s)",
			biosDate, biosRegionLetter, regionName(biosRegionLetter)
		);
		printString(ctx, 24, 42, 0xffffff, line);
		printString(ctx, 24, 52, 0xffffff, biosVersion);

		printString(ctx, 16, 218, 0x505050, "Any button: return to menu");

		endFrame(ctx);
	}
	// Flush button state before handing control back to the outer menu
	// system - see the matching comment in memcard.c's
	// runMemoryCardManager() for the full explanation. Without this,
	// whichever button you exited with is still held when control
	// returns, and the outer menu's stale lastButtons misreads it as a
	// fresh "confirm" press on this same still-highlighted item,
	// immediately re-opening this screen.
	while (pollController(0) | pollController(1))
		;

	// Deliberately no enterToolsMenu()/enterMainMenu() call here -
	// state->currentMenu and state->menuCursor were never touched
	// anywhere in this function (this screen renders through its own
	// self-contained loop instead), so they still correctly point at
	// whichever menu and item were selected to get here.
}

/* ---- BIOS memory dump (diagnostic) ---- */

#define DUMP_ROWS_PER_PAGE 8
#define DUMP_NUM_PAGES      3 // 3 * 8 = 24 rows = 192 bytes covered

void runBiosMemoryDump(
	RenderContext  *ctx,
	UIState        *state,
	const MenuItem *item
) {
	(void) state;
	(void) item;

	const uint8_t *base = (const uint8_t *) 0xbfc00100;
	int            page = 0;

	char line[64];

	// Debounce: wait for the button that opened this screen to be released.
	while (pollController(0) | pollController(1))
		;

	uint16_t lastButtons = 0;

	for (;;) {
		uint16_t buttons = pollController(0) | pollController(1);
		uint16_t pressed = buttons & ~lastButtons;
		lastButtons       = buttons;

		if (pressed & PAD_BTN_LEFT)
			page = (page + DUMP_NUM_PAGES - 1) % DUMP_NUM_PAGES;
		if (pressed & PAD_BTN_RIGHT)
			page = (page + 1) % DUMP_NUM_PAGES;
		if (pressed & (PAD_BTN_START | PAD_BTN_CIRCLE | PAD_BTN_CROSS))
			break;

		beginFrame(ctx);
		drawBackground(ctx);

		snprintf(
			line, sizeof(line), "BIOS MEMORY DUMP (page %d/%d)",
			page + 1, DUMP_NUM_PAGES
		);
		printString(ctx, 16, 8, 0x808080, line);

		for (int row = 0; row < DUMP_ROWS_PER_PAGE; row++) {
			int offset = (page * DUMP_ROWS_PER_PAGE + row) * 8;
			const uint8_t *p = base + offset;

			int pos = 0;
			pos += snprintf(
				line + pos, sizeof(line) - pos, "%04X: ", 0x100 + offset
			);
			for (int i = 0; i < 8; i++)
				pos += snprintf(line + pos, sizeof(line) - pos, "%02X ", p[i]);

			line[pos++] = ' ';
			for (int i = 0; i < 8; i++) {
				uint8_t c = p[i];
				line[pos++] = ((c >= 0x20) && (c < 0x7f)) ? (char) c : '.';
			}
			line[pos] = '\0';

			printString(ctx, 16, 22 + row * 12, 0xffffff, line);
		}

		printString(ctx, 16, 200, 0x505050, "LEFT/RIGHT: change page");
		printString(ctx, 16, 212, 0x505050, "START/O/X: return to menu");

		endFrame(ctx);
	}
	// Flush button state before handing control back to the outer menu
	// system - see the matching comment in memcard.c's
	// runMemoryCardManager() for the full explanation. Without this,
	// whichever button you exited with is still held when control
	// returns, and the outer menu's stale lastButtons misreads it as a
	// fresh "confirm" press on this same still-highlighted item,
	// immediately re-opening this screen.
	while (pollController(0) | pollController(1))
		;

	// Deliberately no enterToolsMenu()/enterMainMenu() call here -
	// state->currentMenu and state->menuCursor were never touched
	// anywhere in this function (this screen renders through its own
	// self-contained loop instead), so they still correctly point at
	// whichever menu and item were selected to get here.
}

/* ---- CD-ROM controller (Mechacon) version -> motherboard revision ----
 *
 * REBUILT against real, production reference code: tonyhax
 * (github.com/socram8888/tonyhax), a save-exploit loader that has to work
 * reliably across essentially every real PS1 in the wild - about as
 * battle-tested as PS1 bare-metal code gets. Comparing our earlier
 * implementation against their cd_command()/cd_wait_int() revealed a real,
 * likely-causal bug: we never waited for BUSYSTS (bit 7 of the status
 * register) to clear before sending a new command or before polling for
 * the response interrupt. tonyhax does this as the very first step in
 * both functions. We also only cleared the parameter FIFO (0x40) where
 * tonyhax clears both the parameter FIFO AND acknowledges BUSYSTS
 * together (0xC0). Missing synchronization like this is exactly the kind
 * of thing that a lenient emulator can tolerate while real hardware
 * can't - a very plausible explanation for instability that only showed
 * up on real consoles.
 *
 * Also confirmed independently correct by this reference: reading the
 * response FIFO while bit 5 (RSLRRDY) is set, which is what our own
 * fix already landed on.
 */

#define CDROM_REG0 (*(volatile uint8_t *) 0x1f801800) // status/index
#define CDROM_REG1 (*(volatile uint8_t *) 0x1f801801) // command / response fifo
#define CDROM_REG2 (*(volatile uint8_t *) 0x1f801802) // parameter fifo / data fifo
#define CDROM_REG3 (*(volatile uint8_t *) 0x1f801803) // request / interrupt enable/flag

#define CDROM_STAT_RSLRRDY (1 << 5)
#define CDROM_STAT_BUSYSTS (1 << 7)

static void cdromSendCommand(uint8_t cmd, const uint8_t *params, int numParams) {
	// Wait for any previous command to finish - this was the missing step.
	while (CDROM_REG0 & CDROM_STAT_BUSYSTS)
		;

	CDROM_REG0 = 0; // page 0

	// Clear the parameter FIFO AND acknowledge BUSYSTS together (0xC0),
	// not just the parameter FIFO alone (0x40, what we had before).
	CDROM_REG3 = 0xc0;

	for (int i = 0; i < numParams; i++)
		CDROM_REG2 = params[i];

	CDROM_REG0 = 1;    // page 1
	CDROM_REG2 = 0x00; // disable IRQ generation - we're polling manually
	CDROM_REG3 = 0x07; // acknowledge any pending interrupt flags
	CDROM_REG0 = 0;    // back to page 0

	CDROM_REG1 = cmd; // trigger the command
}

// Returns the interrupt code (1-7), or -1 on timeout.
static int cdromWaitForInterrupt(void) {
	while (CDROM_REG0 & CDROM_STAT_BUSYSTS)
		;

	CDROM_REG0 = 1;

	uint32_t timeout = 2000000;
	uint8_t  flags;
	do {
		flags = CDROM_REG3 & 0x07;
		if (--timeout == 0)
			return -1;
	} while (flags == 0);

	CDROM_REG3 = 0x07; // acknowledge
	return flags;
}

static int cdromReadResponse(uint8_t *out, int maxBytes) {
	CDROM_REG0 = 1;

	int count = 0;
	while ((count < maxBytes) && (CDROM_REG0 & CDROM_STAT_RSLRRDY))
		out[count++] = CDROM_REG1;

	return count;
}

typedef struct {
	uint8_t     bytes[4];
	const char  *motherboard;
	const char  *date;
} MechaconEntry;

// Taken directly from psx-spx's own published Mechacon version table, plus
// one entry confirmed against real hardware (95 09 12 C2).
static const MechaconEntry MECHACON_TABLE[] = {
	{ { 0x94, 0x09, 0x19, 0xc0 }, "PU-7",          "1994-09-19" },
	{ { 0x94, 0x11, 0x18, 0xc0 }, "PU-7",          "1994-11-18" },
	{ { 0x95, 0x05, 0x16, 0xc1 }, "LATE-PU-8",     "1995-05-16" },
	{ { 0x95, 0x07, 0x24, 0xc1 }, "LATE-PU-8",     "1995-07-24" },
	{ { 0x95, 0x09, 0x12, 0xc2 }, "LATE-PU-8",     "1995-09-12" },
	{ { 0x96, 0x08, 0x15, 0xc2 }, "PU-16 (VCD)",   "1996-08-15" },
	{ { 0x96, 0x08, 0x18, 0xc1 }, "LATE-PU-8",     "1996-08-18" },
	{ { 0x96, 0x09, 0x12, 0xc2 }, "PU-18 (JP)",    "1996-09-12" },
	{ { 0x97, 0x01, 0x10, 0xc2 }, "PU-18",         "1997-01-10" },
	{ { 0x97, 0x08, 0x14, 0xc2 }, "PU-20",         "1997-08-14" },
	{ { 0x98, 0x06, 0x10, 0xc3 }, "PU-22",         "1998-06-10" },
	{ { 0x99, 0x02, 0x01, 0xc3 }, "PU-23 / PM-41", "1999-02-01" },
	{ { 0xa1, 0x03, 0x06, 0xc3 }, "PM-41 (later)", "2001-06-06" },
};

#define NUM_MECHACON_ENTRIES (sizeof(MECHACON_TABLE) / sizeof(MECHACON_TABLE[0]))

// Returns true on success. intCode reports what actually happened (3 =
// success, other INT codes or -1 for timeout = something went wrong) so
// the display can show it even on failure, for diagnostic purposes.
static bool getCDROMVersion(uint8_t out[4], int *intCode) {
	uint8_t param = 0x20; // GetROM sub-function

	cdromSendCommand(0x19, &param, 1);
	int interrupt = cdromWaitForInterrupt();
	*intCode = interrupt;

	if (interrupt < 0) {
		out[0] = out[1] = out[2] = out[3] = 0;
		return false;
	}

	uint8_t raw[16];
	int     count = cdromReadResponse(raw, sizeof(raw));

	if ((interrupt != 3) || (count != 4)) {
		out[0] = out[1] = out[2] = out[3] = 0;
		return false;
	}

	out[0] = raw[0];
	out[1] = raw[1];
	out[2] = raw[2];
	out[3] = raw[3];
	return true;
}

static const MechaconEntry *lookupMechacon(const uint8_t bytes[4]) {
	for (size_t i = 0; i < NUM_MECHACON_ENTRIES; i++) {
		const uint8_t *entry = MECHACON_TABLE[i].bytes;

		if (
			(entry[0] == bytes[0]) && (entry[1] == bytes[1]) &&
			(entry[2] == bytes[2]) && (entry[3] == bytes[3])
		)
			return &MECHACON_TABLE[i];
	}

	return NULL;
}

/* ---- CD unlock debug screen ----
 *
 * Runs the SecretUnlock sequence step by step using the same proven CD
 * command helpers as the rest of this file, capturing the interrupt code
 * and response bytes for every command so we can see on real hardware
 * exactly where the CD-R unlock is failing. Read-only diagnostic: it
 * does NOT reboot, so you can note the values and back out.
 *
 * Key lines to read:
 *  - "CD ver": last byte must be C1 or higher, else the 0x50-0x56
 *    SecretUnlock commands don't exist on this controller.
 *  - Each "50".."56" line: INT should be 5 with bytes "11 40" (that IS
 *    the documented success response). INT 5 with "11 10" means the
 *    command/params were rejected.
 *  - "GetID after": bytes 5-8 are the SCEx region. "53 43 45 4x"
 *    (="SCEx") means the drive is now UNLOCKED; "00 00 00 00" means it's
 *    still locked (unlock didn't take).
 */

static void fmtBytesInline(char *out, size_t outSize, const uint8_t *b, int n) {
	int pos = 0;
	for (int i = 0; (i < n) && (pos + 3 < (int) outSize); i++)
		pos += snprintf(out + pos, outSize - pos, "%02X ", b[i]);
	if (pos == 0 && outSize)
		out[0] = 0;
}

// Runs one CD command; returns interrupt code (or -1 on timeout) and
// fills resp[]/*count with the first response.
static int cdDbgCommand(
	uint8_t cmd, const uint8_t *params, int numParams,
	uint8_t *resp, int maxResp, int *count
) {
	cdromSendCommand(cmd, params, numParams);

	int intc = cdromWaitForInterrupt();
	int n    = 0;

	if (intc >= 0)
		n = cdromReadResponse(resp, maxResp);

	if (count)
		*count = n;
	return intc;
}

// Waits for and reads a command's SECOND response (e.g. GetID's INT2/INT5
// following its INT3 acknowledge).
static int cdDbgSecondResponse(uint8_t *resp, int maxResp, int *count) {
	int intc = cdromWaitForInterrupt();
	int n    = 0;

	if (intc >= 0)
		n = cdromReadResponse(resp, maxResp);

	if (count)
		*count = n;
	return intc;
}

/* ---- UniROM-accurate unlock, replicated from disassembly ----
 *
 * These use the uncached (KSEG1, 0xBF80xxxx) CD registers and mirror
 * UniROM's exact sequence: a full CD init (ack IRQs, HCHPCTL=0, set
 * Com_Delay=0x1325, Nop, Nop, Init) BEFORE the 0x50..0x56 SecretUnlock
 * commands, with a per-command pre-step that waits for DRQSTS+BUSYSTS and
 * acknowledges all IRQ flags with 0x1F. The earlier version skipped the
 * init entirely, which is why the commands were accepted but the drive
 * never actually unlocked.
 */

#define CDU_REG0     (*(volatile uint8_t  *) 0xbf801800)
#define CDU_REG1     (*(volatile uint8_t  *) 0xbf801801)
#define CDU_REG2     (*(volatile uint8_t  *) 0xbf801802)
#define CDU_REG3     (*(volatile uint8_t  *) 0xbf801803)
#define CDU_COMDELAY (*(volatile uint32_t *) 0xbf801020)

// UniROM's per-command pre-step (sub_E1EF0): wait for the controller to
// go idle, then acknowledge/clear all pending IRQ flags. Leaves bank 0
// selected so parameters/command can be written next.
static void cduPreStep(void) {
	while (CDU_REG0 & 0x40) // DRQSTS
		;
	while (CDU_REG0 & 0x80) // BUSYSTS
		;

	CDU_REG0 = 1;    // bank 1
	CDU_REG3 = 0x1f; // HCLRCTL: acknowledge all IRQ flags
	CDU_REG0 = 0;    // bank 0
}

static int cduWaitIRQ(void) {
	uint32_t timeout = 2000000;
	uint8_t  flags;

	CDU_REG0 = 1; // bank 1 to read HINTSTS
	do {
		flags = CDU_REG3 & 0x07;
		if (--timeout == 0)
			return -1;
	} while (flags == 0);

	return flags;
}

static int cduReadResponse(uint8_t *out, int maxBytes) {
	CDU_REG0 = 1;

	int n = 0;
	while ((n < maxBytes) && (CDU_REG0 & 0x20)) // RSLRRDY
		out[n++] = CDU_REG1;

	return n;
}

// Send command + params, wait first response, read it, then ack. Returns
// the interrupt code (-1 on timeout).
static int cduCommand(
	uint8_t cmd, const char *paramStr,
	uint8_t *resp, int maxResp, int *count
) {
	cduPreStep();

	// Parameters (bank 0, PARAMETER FIFO) before the command byte.
	if (paramStr)
		for (const char *p = paramStr; *p; p++)
			CDU_REG2 = (uint8_t) *p;

	CDU_REG1 = cmd; // COMMAND

	int intc = cduWaitIRQ();
	int n    = 0;
	if (intc >= 0)
		n = cduReadResponse(resp, maxResp);

	// Acknowledge all IRQ flags (bank 1).
	CDU_REG0 = 1;
	CDU_REG3 = 0x1f;

	if (count)
		*count = n;
	return intc;
}

// Full UniROM-style init + SecretUnlock. Region string is "of America"
// for a US console (matches UniROM). Fills the per-command result strings
// for display.
static void cduRunUnlock(char unlockLines[7][40]) {
	uint8_t resp[16];
	int     count;

	// --- CD init (matches UniROM / psx-spx "To init the CD") ---
	CDU_REG0 = 1;    CDU_REG3 = 0x1f; // ack all IRQs
	CDU_REG0 = 0;    CDU_REG3 = 0x00; // HCHPCTL = 0
	CDU_COMDELAY = 0x1325;            // Com_Delay = 4901

	cduCommand(0x01, NULL, resp, sizeof(resp), &count); // Nop
	cduCommand(0x01, NULL, resp, sizeof(resp), &count); // Nop
	cduCommand(0x0a, NULL, resp, sizeof(resp), &count); // Init (INT3)
	cduWaitIRQ();                                        // Init 2nd resp (INT2)
	CDU_REG0 = 1; CDU_REG3 = 0x1f;

	// --- SecretUnlock 0x50..0x56 ---
	static const struct { uint8_t cmd; const char *text; } parts[7] = {
		{ 0x50, NULL },          { 0x51, "Licensed by" },
		{ 0x52, "Sony" },        { 0x53, "Computer" },
		{ 0x54, "Entertainment" }, { 0x55, "of America" },
		{ 0x56, NULL }
	};

	for (int i = 0; i < 7; i++) {
		int intc = cduCommand(parts[i].cmd, parts[i].text, resp, sizeof(resp), &count);
		char hex[24];
		fmtBytesInline(hex, sizeof(hex), resp, count);
		snprintf(unlockLines[i], sizeof(unlockLines[i]),
			"%02X: INT%d %s", parts[i].cmd, intc, hex);
	}
}

void runCDUnlockDebug(
	RenderContext  *ctx,
	UIState        *state,
	const MenuItem *item
) {
	(void) state;
	(void) item;

	char cdVer[44], idBefore[64], idAfter[64], scexLine[40];
	char unlockLines[7][40];

	uint8_t resp[16];
	int     count;

	// 1. CD controller version (uses the shared helper; read-only).
	{
		uint8_t p = 0x20;
		int intc = cdDbgCommand(0x19, &p, 1, resp, sizeof(resp), &count);
		if (intc == 3 && count >= 4)
			snprintf(cdVer, sizeof(cdVer), "CD ver: %02X %02X %02X %02X (need>=C1)",
				resp[0], resp[1], resp[2], resp[3]);
		else
			snprintf(cdVer, sizeof(cdVer), "CD ver: FAILED (INT %d)", intc);
	}

	// 2. GetID BEFORE unlock.
	{
		cdDbgCommand(0x1a, NULL, 0, resp, sizeof(resp), &count);
		int intc = cdDbgSecondResponse(resp, sizeof(resp), &count);
		char hex[40]; fmtBytesInline(hex, sizeof(hex), resp, count);
		snprintf(idBefore, sizeof(idBefore), "GetID before: INT%d %s", intc, hex);
	}

	// 3. Full UniROM-style init + unlock.
	cduRunUnlock(unlockLines);

	// 4. GetID AFTER unlock - the definitive check.
	{
		cduCommand(0x1a, NULL, resp, sizeof(resp), &count);        // INT3 ack
		int intc = cduWaitIRQ();
		int n = 0;
		if (intc >= 0) n = cduReadResponse(resp, sizeof(resp));
		CDU_REG0 = 1; CDU_REG3 = 0x1f;
		char hex[40]; fmtBytesInline(hex, sizeof(hex), resp, n);
		snprintf(idAfter, sizeof(idAfter), "GetID after:  INT%d %s", intc, hex);
	}

	// 5. SCEx counters.
	{
		uint8_t p = 0x05;
		int intc = cdDbgCommand(0x19, &p, 1, resp, sizeof(resp), &count);
		if (intc == 3 && count >= 2)
			snprintf(scexLine, sizeof(scexLine), "SCEx: total=%d success=%d", resp[0], resp[1]);
		else
			snprintf(scexLine, sizeof(scexLine), "SCEx: FAILED (INT %d)", intc);
	}

	while (pollController(0) | pollController(1))
		;

	for (;;) {
		if (pollController(0) | pollController(1))
			break;

		beginFrame(ctx);
		drawBackground(ctx);

		int y = 14;
		printString(ctx, 16, y, 0xffffff, "CD UNLOCK DEBUG (UniROM seq)"); y += 14;
		printString(ctx, 16, y, 0xffff80, cdVer); y += 13;
		printString(ctx, 16, y, 0x80ff80, idBefore); y += 14;

		printString(ctx, 16, y, 0x808080, "SecretUnlock (INT5 xx 40 expected):"); y += 12;
		for (int i = 0; i < 7; i++) {
			printString(ctx, 24, y, 0xffffff, unlockLines[i]); y += 11;
		}
		y += 3;
		printString(ctx, 16, y, 0x80ffff, idAfter); y += 12;
		printString(ctx, 16, y, 0x808080, "  bytes 5-8 = 53 43 45 4x means UNLOCKED"); y += 12;
		printString(ctx, 16, y, 0xffff80, scexLine); y += 12;

		printString(ctx, 16, 226, 0x505050, "Any button: return to menu");

		endFrame(ctx);
	}

	while (pollController(0) | pollController(1))
		;
}

void runCDROMTest(
	RenderContext  *ctx,
	UIState        *state,
	const MenuItem *item
) {
	(void) state;
	(void) item;

	uint8_t mechacon[4];
	int     intCode;
	bool    ok = getCDROMVersion(mechacon, &intCode);
	const MechaconEntry *mechaconInfo = ok ? lookupMechacon(mechacon) : NULL;

	char line[64];

	while (pollController(0) | pollController(1))
		;

	for (;;) {
		if (pollController(0) | pollController(1))
			break;

		beginFrame(ctx);
		drawBackground(ctx);

		printString(ctx, 16, 20, 0x808080, "CD-ROM CONTROLLER TEST");

		snprintf(line, sizeof(line), "Interrupt code: %d (expect 3)", intCode);
		printString(ctx, 24, 40, 0xffffff, line);

		if (ok) {
			snprintf(
				line, sizeof(line), "Raw: %02X %02X %02X %02X",
				mechacon[0], mechacon[1], mechacon[2], mechacon[3]
			);
			printString(ctx, 24, 52, 0xffffff, line);

			if (mechaconInfo) {
				snprintf(
					line, sizeof(line), "Motherboard: %s (%s)",
					mechaconInfo->motherboard, mechaconInfo->date
				);
				printString(ctx, 24, 64, 0xffffff, line);
			} else {
				printString(ctx, 24, 64, 0x808080, "Motherboard: unrecognized version");
			}
		} else {
			printString(
				ctx, 24, 52, 0x808080,
				(intCode < 0) ? "Timed out waiting for interrupt" : "Command did not succeed"
			);
		}

		printString(ctx, 16, 218, 0x505050, "Any button: return to menu");

		endFrame(ctx);
	}
	// Flush button state before handing control back to the outer menu
	// system - see the matching comment in memcard.c's
	// runMemoryCardManager() for the full explanation. Without this,
	// whichever button you exited with is still held when control
	// returns, and the outer menu's stale lastButtons misreads it as a
	// fresh "confirm" press on this same still-highlighted item,
	// immediately re-opening this screen.
	while (pollController(0) | pollController(1))
		;

	// Deliberately no enterToolsMenu()/enterMainMenu() call here -
	// state->currentMenu and state->menuCursor were never touched
	// anywhere in this function (this screen renders through its own
	// self-contained loop instead), so they still correctly point at
	// whichever menu and item were selected to get here.
}

/* ---- Main RAM size (fold test) ----
 *
 * Standard "memory fold" test, extended to check multiple boundaries
 * instead of just the usual 2MB-vs-8MB check: write a marker to the very
 * start of RAM, then a different marker at each candidate size boundary
 * in increasing order. If a given boundary is beyond the console's real,
 * physically wired memory, that write "folds" back and overwrites the
 * start of RAM too (the address bus doesn't have enough lines to
 * distinguish it), which we detect by reading the start value back. The
 * first boundary where a fold shows up is the real RAM size - larger
 * boundaries aren't tested once that's found, since they'd fold for the
 * same reason.
 *
 * This is NOT the same kind of test as ps1-ram-tester's own RAM test -
 * that one is designed to find bad/failing memory cells and needs many
 * passes with different bit patterns to catch intermittent faults. This
 * is answering a completely different question ("how much RAM is
 * physically here"), which only takes a handful of writes/reads, no
 * passes needed at all.
 */

/* ---- Main RAM size ----
 *
 * REPLACED the earlier memory-fold approach entirely - it was based on a
 * wrong assumption. The PS1 memory controller's address folding isn't
 * purely a function of which RAM chips are physically installed; it's
 * governed by the DRAM_CTRL configuration register, which the BIOS sets
 * up at boot. That register can be WRONG relative to what's physically
 * there - ramconfig.c's own fixRetailRAMConfig() comment says exactly
 * this: "The retail BIOS accidentally configures main RAM as 8 MB." That
 * mismatch is what caused both symptoms: DuckStation showing 16MB (no
 * real fold ever occurred, since the controller was configured for more
 * space than physically exists) and real hardware crashing (writing into
 * that "phantom" region isn't a safe fold, it's addresses with no real
 * memory chip behind them).
 *
 * The actual correct approach, already proven on real hardware: read
 * DRAM_CTRL directly, exactly what ps1-ram-tester's own getMainRAMSize()
 * already does (see ramconfig.c) - a plain status register read, no
 * memory writes at all, so there's nothing left to corrupt.
 */

static uint32_t getRAMSizeMB(void) {
	return (uint32_t) (getMainRAMSize() / 0x100000);
}

/* ---- GPU version (needed to pick the correct 16MB-unlock value) ---- */

static bool isNewGPU(void) {
	GPU_GP1 = 0x10000007; // GP1(10h), index 7

	return GPU_GP0 == 2; // GPUREAD, same physical register as GP0
}

/* ---- 16MB RAM unlock ----
 *
 * Community-sourced technique from a real PS1 RAM-modding hobbyist,
 * independently cross-checked against our own pre-existing DRAM_CTRL
 * decode formula (getMainRAMSize() above) before trusting it: writing
 * 0x0FAC (later-GPU boards) or 0x0FA4 (early-GPU boards) to DRAM_CTRL
 * both decode to exactly 16MB through that same formula - real,
 * independent confirmation this is accurate, not taken on faith. Also
 * confirmed directly: a real 16MB-modded console reported
 * DRAM_CTRL=0x8C430988 (decodes to 2MB) before this write - the stock
 * kernel never configures DRAM_CTRL for 16MB on its own, it has to be
 * written explicitly.
 *
 * This writes to a configuration register - the same one
 * fixRetailRAMConfig() already safely writes to, just a different value
 * - not raw memory, so it's inherently safer than the earlier raw fold
 * test that crashed by touching kernel-reserved memory. Still: telling
 * the memory controller there's more RAM than a normal console has is
 * not risk-free if that RAM genuinely isn't there, so this verifies with
 * a proper fold-detection check (not just a write/readback, which could
 * give a false positive if the write happens to fold onto an
 * undisturbed address) before trusting the change, and always restores
 * the original value if verification fails.
 */

// Writes 6 distinct values across 6 addresses spread through the given
// region, then checks all of them independently retained their own
// distinct value - if they're secretly all aliasing to the same
// physical location, only the last value written would survive
// everywhere, so six different addresses can't coincidentally all pass
// unless genuinely backed by independent memory.
static bool verifyDistinctRegion(const uint32_t offsets[6]) {
	static const uint32_t patterns[6] = {
		0x11111111, 0x22222222, 0x33333333,
		0x44444444, 0x55555555, 0x66666666
	};

	volatile uint32_t *ptrs[6];
	uint32_t           saved[6];

	for (int i = 0; i < 6; i++) {
		ptrs[i]  = (volatile uint32_t *) (0x80000000 + offsets[i]);
		saved[i] = *ptrs[i];
	}

	for (int i = 0; i < 6; i++)
		*ptrs[i] = patterns[i];

	bool allDistinct = true;
	for (int i = 0; i < 6; i++) {
		if (*ptrs[i] != patterns[i]) {
			allDistinct = false;
			break;
		}
	}

	for (int i = 0; i < 6; i++)
		*ptrs[i] = saved[i];

	return allDistinct;
}

// Addresses spread through the claimed 8-16MB extension (for testing the
// 16MB unlock) and through the claimed 2-8MB extension (for testing
// plain 8MB), each safely clear of the boundary edges on either side.
static const uint32_t REGION_8_TO_16MB[6] = {
	0x00900000, 0x00a00000, 0x00b00000,
	0x00d00000, 0x00e00000, 0x00e80000
};
static const uint32_t REGION_2_TO_8MB[6] = {
	0x00300000, 0x00380000, 0x00400000,
	0x00500000, 0x00600000, 0x00700000
};

// Sequential test: try claiming 16MB first, then 8MB, falling back to
// the always-valid 2MB baseline - each tier verified independently
// before being trusted.
//
// FIXED a real bug here: the first version wrote hardcoded, size-only
// values for the 8MB and 2MB-fallback steps (DRAM_CTRL_SIZE_8MB |
// DRAM_CTRL_BANKS_1, etc.), which strips out the OTHER bits in the
// register - and DRAM_CTRL doesn't just encode size, it also encodes
// memory timing parameters specific to the actual RAM chips on that
// board. Overwriting those with a size-only reconstruction destroys the
// real timing configuration the BIOS originally set up. An emulator
// doesn't model those timing bits at all, so this passed fine on
// DuckStation - but real hardware genuinely needs them, which is why it
// crashed there. Same principle fixRetailRAMConfig() already follows:
// read-modify-write, only touching the size/bank bits, preserving
// everything else. The 16MB magic values are exempt from this - your
// friend's values already include the correct timing bits for that
// specific configuration, confirmed earlier by cross-checking their
// size-decode against our own formula.
static uint32_t tryProgressiveRAMDetect(void) {
	uint32_t original = DRAM_CTRL;

	DRAM_CTRL = isNewGPU() ? 0x0fac : 0x0fa4;
	if (verifyDistinctRegion(REGION_8_TO_16MB))
		return 16;

	DRAM_CTRL = (original & ~(DRAM_CTRL_SIZE_BITMASK | DRAM_CTRL_BANKS_BITMASK))
	          | DRAM_CTRL_SIZE_8MB | DRAM_CTRL_BANKS_1;
	if (verifyDistinctRegion(REGION_2_TO_8MB))
		return 8;

	// Nothing beyond baseline verified - restore the EXACT original
	// value, not a reconstructed one.
	DRAM_CTRL = original;
	return 2;
}

void runRAMTest(
	RenderContext  *ctx,
	UIState        *state,
	const MenuItem *item
) {
	(void) state;
	(void) item;

	uint32_t sizeMB    = getRAMSizeMB();
	uint32_t dramCtrl  = DRAM_CTRL;

	bool triedProgressive = false;

	char     line[64];
	uint16_t lastButtons = 0;

	while (pollController(0) | pollController(1))
		;

	for (;;) {
		uint16_t buttons = pollController(0) | pollController(1);
		uint16_t pressed = buttons & ~lastButtons;
		lastButtons       = buttons;

		if (pressed & PAD_BTN_CROSS) {
			sizeMB            = tryProgressiveRAMDetect();
			dramCtrl          = DRAM_CTRL;
			triedProgressive  = true;
		}

		if (pressed & PAD_BTN_CIRCLE)
			break;

		beginFrame(ctx);
		drawBackground(ctx);

		printString(ctx, 16, 20, 0x808080, "MAIN RAM SIZE TEST");

		snprintf(line, sizeof(line), "Detected: %u MB", (unsigned int) sizeMB);
		printString(ctx, 24, 40, 0xffffff, line);

		snprintf(line, sizeof(line), "Raw DRAM_CTRL: %08X", (unsigned int) dramCtrl);
		printString(ctx, 24, 52, 0xffffff, line);

		const char *note;
		switch (sizeMB) {
			case 2:  note = "Standard retail configuration";       break;
			case 8:  note = "8 MB dev/Yaroze-style configuration"; break;
			case 16: note = "16 MB (non-standard mod)";            break;
			default: note = "Non-standard configuration";          break;
		}
		printString(ctx, 24, 64, 0x808080, note);

		if (triedProgressive) {
			printString(
				ctx, 24, 80, 0x1256e3,
				"Actively verified (16MB -> 8MB -> 2MB sequence)"
			);
		}

		printString(
			ctx, 16, 190, 0x505050,
			CH_PS1_CROSS_BUTTON " Actively test 16/8/2 MB (experimental)"
		);
		printString(
			ctx, 16, 202, 0x505050,
			"Writes DRAM_CTRL directly - see console_info.c for details"
		);
		printString(
			ctx, 16, 218, 0x505050,
			CH_PS1_CIRCLE_BUTTON " return to menu"
		);

		endFrame(ctx);
	}
	// Flush button state before handing control back to the outer menu
	// system - see the matching comment in memcard.c's
	// runMemoryCardManager() for the full explanation. Without this,
	// whichever button you exited with is still held when control
	// returns, and the outer menu's stale lastButtons misreads it as a
	// fresh "confirm" press on this same still-highlighted item,
	// immediately re-opening this screen.
	while (pollController(0) | pollController(1))
		;

	// Deliberately no enterToolsMenu()/enterMainMenu() call here -
	// state->currentMenu and state->menuCursor were never touched
	// anywhere in this function (this screen renders through its own
	// self-contained loop instead), so they still correctly point at
	// whichever menu and item were selected to get here.
}

/* ---- GPU version test (standalone) ---- */

void runGPUVersionTest(
	RenderContext  *ctx,
	UIState        *state,
	const MenuItem *item
) {
	(void) state;
	(void) item;

	GPU_GP1 = 0x10000007; // GP1(10h), index 7
	uint32_t raw = GPU_GP0; // GPUREAD, same physical register as GP0
	bool     isNew = (raw == 2);

	char line[64];

	while (pollController(0) | pollController(1))
		;

	for (;;) {
		if (pollController(0) | pollController(1))
			break;

		beginFrame(ctx);
		drawBackground(ctx);

		printString(ctx, 16, 20, 0x808080, "GPU VERSION TEST");

		snprintf(line, sizeof(line), "Raw GP1(10h:7): %08X", (unsigned int) raw);
		printString(ctx, 24, 40, 0xffffff, line);

		printString(
			ctx, 24, 52, 0xffffff,
			isNew
				? "208-pin GPU (LATE-PU-8 and up)"
				: "160-pin GPU, most likely (EARLY-PU-8 and below)"
		);

		snprintf(
			line, sizeof(line), "16MB unlock value for this GPU: %04X",
			isNew ? 0x0fac : 0x0fa4
		);
		printString(ctx, 24, 68, 0x808080, line);

		printString(ctx, 16, 218, 0x505050, "Any button: return to menu");

		endFrame(ctx);
	}
	// Flush button state before handing control back to the outer menu
	// system - see the matching comment in memcard.c's
	// runMemoryCardManager() for the full explanation. Without this,
	// whichever button you exited with is still held when control
	// returns, and the outer menu's stale lastButtons misreads it as a
	// fresh "confirm" press on this same still-highlighted item,
	// immediately re-opening this screen.
	while (pollController(0) | pollController(1))
		;

	// Deliberately no enterToolsMenu()/enterMainMenu() call here -
	// state->currentMenu and state->menuCursor were never touched
	// anywhere in this function (this screen renders through its own
	// self-contained loop instead), so they still correctly point at
	// whichever menu and item were selected to get here.
}
