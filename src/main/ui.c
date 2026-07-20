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

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "common/sio0.h"
#include "main/defs.h"
#include "main/font.h"
#include "main/renderer.h"
#include "main/sound.h"
#include "main/ui.h"

#define MARGIN_LEFT   16
#define MARGIN_RIGHT  16
#define MARGIN_TOP    14
#define MARGIN_BOTTOM  8

#define ITEM_HEIGHT        (FONT_LINE_HEIGHT + 4)
#define STATIC_ITEM_HEIGHT (FONT_LINE_HEIGHT + 1)
#define SEPARATOR_HEIGHT   (ITEM_HEIGHT / 2)
#define HIGHLIGHT_PADDING  2

#define PROGRESS_BAR_HEIGHT  10
#define PROGRESS_BAR_SPACING  8

#define BUTTON_REPEAT_DELAY    30
#define BUTTON_REPEAT_INTERVAL 10

// Green terminal theme. Two kinds of values live here:
//  - Colors used by drawRect() are UNtextured, so the value is the literal
//    on-screen RGB (0xRRGGBB): e.g. COLOR_HIGHLIGHT1 is bright green.
//  - Colors used by printString()/drawChar() MULTIPLY the (white) font
//    texture, so on-screen result is roughly 2x the value, clamped. The
//    text values below are chosen so white glyphs come out the intended
//    green (e.g. COLOR_TEXT1 -> ~(0,255,64)).
enum Color {
	// Literal rect colors
	COLOR_WINDOW1     = 0x004018, // frame / panel mid   (0, 64, 24)
	COLOR_WINDOW2     = 0x002010, // frame / panel dark  (0, 32, 16)
	COLOR_WINDOW3     = 0x000A05, // near-black green     (0, 10,  5)
	COLOR_HIGHLIGHT1  = 0x00FF41, // selection bar        (0,255, 65)
	COLOR_HIGHLIGHT2  = 0x00B030, // value sub-band       (0,176, 48)
	COLOR_PROGRESS1   = 0x00FF41, // progress bright
	COLOR_PROGRESS2   = 0x006020, // progress dark

	// Text multiply colors (applied to white glyphs)
	COLOR_TEXT1       = 0x008020, // bright green text -> ~(0,255, 64)
	COLOR_TEXT2       = 0x005014, // dim green text    -> ~(0,160, 40)
	COLOR_TEXT3       = 0x00370E, // footer/faint      -> ~(0,110, 28)
	COLOR_TEXT_ACTIVE = 0x3C803C, // whiter green      -> ~(120,255,120)
	COLOR_SEL_TEXT    = 0x001405  // dark text on the bright bar -> ~(0,40,10)
};

/* Utilities */

static void moveMenuCursor(UIState *state, int step) {
	int             newCursor = state->menuCursor;
	const MenuItem *newItem   = &state->currentMenu[newCursor];

	// This is very crude and will break if step is not 1 or -1.
	do {
		newCursor += step;
		newItem   += step;

		if (newCursor < 0)
			return; // clamp at the top, no wrap-around
		if (newItem->type == ITEM_END)
			return; // clamp at the bottom, no wrap-around
	} while (newItem->type < ITEM_ACTION);

	state->menuCursor = newCursor;
	playScrollSound();
}

static void drawButtonPrompt(RenderContext *ctx, const char *prompt) {
	const char *version = "psx-itests-v" VERSION_STRING;
	int        promptY  =
		ctx->screenHeight - (MARGIN_BOTTOM + FONT_LINE_HEIGHT);

	printString(ctx, MARGIN_LEFT, promptY, COLOR_TEXT3, prompt);
	printString(
		ctx,
		ctx->screenWidth - (MARGIN_RIGHT + getStringWidth(version)),
		promptY,
		COLOR_TEXT3,
		version
	);
}

static void updateUIState(UIState *state, uint16_t buttons) {
	uint16_t changed        = buttons ^ state->lastButtons;
	state->buttonsPressed   = buttons & changed;
	state->buttonsRepeating = buttons & changed;

	if (buttons && !changed) {
		state->repeatTimer++;

		if (state->repeatTimer >= BUTTON_REPEAT_DELAY) {
			int sinceDelay = state->repeatTimer - BUTTON_REPEAT_DELAY;
			if ((sinceDelay % BUTTON_REPEAT_INTERVAL) == 0)
				state->buttonsRepeating |= buttons;
		}
	} else {
		state->repeatTimer = 0;
	}

	state->lastButtons = buttons;
}

void setupUIState(UIState *state) {
	state->currentMenu      = 0;
	state->buttons          = 0;
	state->lastButtons      = 0;
	state->buttonsPressed   = 0;
	state->buttonsRepeating = 0;
	state->repeatTimer      = 0;
	state->menuCursor       = 0;
}

int findFirstSelectableItem(const MenuItem *menu) {
	int index = 0;

	while ((menu[index].type != ITEM_END) && (menu[index].type < ITEM_ACTION))
		index++;

	return (menu[index].type == ITEM_END) ? 0 : index;
}

/* Menus */

static const char *menuButtonPrompts[] = {
	[ITEM_ACTION] =
		CH_PS1_DPAD " Move   "
		CH_PS1_CIRCLE_BUTTON CH_PS1_CROSS_BUTTON " Select",
	[ITEM_INT]    = CH_PS1_DPAD " Move   " CH_PS1_DPAD_X " Adjust",
	[ITEM_BINARY] = CH_PS1_DPAD " Move   " CH_PS1_DPAD_X " Adjust",
	[ITEM_ENUM]   = CH_PS1_DPAD " Move   " CH_PS1_DPAD_X " Adjust"
};

void renderMenu(RenderContext *ctx, const UIState *state) {
	drawMatrixRain(ctx);

	const MenuItem *item = state->currentMenu;

	int cursorOffset = state->menuCursor;
	int currentY     = MARGIN_TOP;

	while (item->type != ITEM_END) {
		// Determine the item's state.
		uint32_t color  = COLOR_TEXT1;
		int      height = ITEM_HEIGHT;

		const char *value = 0;
		char       buffer[8];

		switch (item->type) {
			case ITEM_SEPARATOR:
				height = SEPARATOR_HEIGHT;
				goto _nextItem;

			case ITEM_TITLE:
				height = STATIC_ITEM_HEIGHT;
				break;

			case ITEM_STATIC:
				color  = COLOR_TEXT2;
				height = STATIC_ITEM_HEIGHT;
				break;

			case ITEM_ACTION:
				if (item->action.tag)
					value = item->action.tag;
				break;

			case ITEM_INT:
				snprintf(buffer, sizeof(buffer), "%d", *item->int_.value);
				value = buffer;
				break;

			case ITEM_BINARY:
				snprintf(
					buffer,
					sizeof(buffer),
					"%0*b",
					item->bitLength,
					*item->int_.value
				);
				value = buffer;
				break;

			case ITEM_ENUM:
				value = item->enum_.items[*item->enum_.value];
				break;

			default:
				assert(false);
		}

		// Draw the item.
		int valueWidth = getStringWidth(value);

		if (!cursorOffset) {
			drawRect(
				ctx,
				MARGIN_LEFT - HIGHLIGHT_PADDING,
				currentY - HIGHLIGHT_PADDING,
				ctx->screenWidth
					- (MARGIN_LEFT + MARGIN_RIGHT - HIGHLIGHT_PADDING * 2),
				height,
				COLOR_HIGHLIGHT1,
				false
			);

			if (valueWidth)
				drawRect(
					ctx,
					ctx->screenWidth
						- (MARGIN_RIGHT + HIGHLIGHT_PADDING * 3 + valueWidth),
					currentY - HIGHLIGHT_PADDING,
					valueWidth + HIGHLIGHT_PADDING * 4,
					height,
					COLOR_HIGHLIGHT2,
					false
				);
		}

		printString(
			ctx, MARGIN_LEFT, currentY,
			cursorOffset ? color : COLOR_SEL_TEXT, item->name
		);
		printString(
			ctx,
			ctx->screenWidth - (MARGIN_RIGHT + valueWidth),
			currentY,
			cursorOffset ? COLOR_TEXT2 : COLOR_SEL_TEXT,
			value
		);

_nextItem:
		item++;
		cursorOffset--;
		currentY += height;
	}

	item = &state->currentMenu[state->menuCursor];

	drawButtonPrompt(ctx, menuButtonPrompts[item->type]);
}

void updateMenu(RenderContext *ctx, UIState *state, uint16_t buttons) {
	updateUIState(state, buttons);

	uint16_t pressed   = state->buttonsPressed;
	uint16_t repeating = state->buttonsRepeating;

	// Handle the currently highlighted item.
	const MenuItem *item = &state->currentMenu[state->menuCursor];
	int            value;

	switch (item->type) {
		case ITEM_ACTION:
			if (pressed & (PAD_BTN_START | PAD_BTN_CIRCLE | PAD_BTN_CROSS)) {
				playConfirmSound();
				item->action.callback(ctx, state, item);
			}
			break;

		case ITEM_INT:
		case ITEM_BINARY:
		case ITEM_ENUM:
			// Note that this assumes item->int_.value aliases to
			// item->enum_.value.
			value = *item->int_.value;

			if (value > item->minValue) {
				if (repeating & PAD_BTN_LEFT)
					*item->int_.value = value - 1;
			} else {
				if (pressed & PAD_BTN_LEFT)
					*item->int_.value = item->maxValue;
			}

			if (value < item->maxValue) {
				if (repeating & PAD_BTN_RIGHT)
					*item->int_.value = value + 1;
			} else {
				if (pressed & PAD_BTN_RIGHT)
					*item->int_.value = item->minValue;
			}
			break;

		default:
			assert(false);
	}

	// Handle menu navigation.
	uint16_t upMask   = !state->menuCursor         ? pressed : repeating;
	uint16_t downMask = (item[1].type == ITEM_END) ? pressed : repeating;

	if (upMask   & PAD_BTN_UP)
		moveMenuCursor(state, -1);
	if (downMask & PAD_BTN_DOWN)
		moveMenuCursor(state,  1);
}

/* Progress bar screen */

void renderProgressScreen(
	RenderContext *ctx,
	int           progress,
	int           total,
	const char    *message
) {
	drawBackground(ctx);

	int textY      = (ctx->screenHeight - PROGRESS_BAR_SPACING) / 2;
	int barY       = (ctx->screenHeight + PROGRESS_BAR_SPACING) / 2;
	int totalWidth = ctx->screenWidth - (MARGIN_LEFT + MARGIN_RIGHT);

	printString(
		ctx,
		MARGIN_LEFT,
		textY - FONT_LINE_HEIGHT,
		COLOR_TEXT1,
		message
	);
	drawRect(
		ctx,
		MARGIN_LEFT,
		barY,
		totalWidth,
		PROGRESS_BAR_HEIGHT,
		COLOR_WINDOW3,
		true
	);
	drawGradientRectH(
		ctx,
		MARGIN_LEFT,
		barY,
		(totalWidth * progress) / total,
		PROGRESS_BAR_HEIGHT,
		COLOR_PROGRESS2,
		COLOR_PROGRESS1,
		false
	);

	drawButtonPrompt(ctx, 0);
}
