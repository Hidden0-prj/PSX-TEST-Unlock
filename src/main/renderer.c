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

#include <stdbool.h>
#include <stdint.h>
#include "common/gpu.h"
#include "main/renderer.h"
#include "main/font.h"
#include "ps1/registers.h"

#define BG_WIDTH         96
#define BG_HEIGHT        48
#define BG_COLOR_DEPTH   GP0_COLOR_4BPP
#define FONT_WIDTH       96
#define FONT_HEIGHT      64
#define FONT_COLOR_DEPTH GP0_COLOR_4BPP

extern const uint8_t bgTexture[],   bgPalette[];
extern const uint8_t fontTexture[], fontPalette[];

/* Renderer management */

void setupRenderer(RenderContext *ctx, int width, int height) {
	ctx->frameCounter = 0;
	ctx->screenWidth  = width;
	ctx->screenHeight = height;

	reloadTextures(ctx);
}

void reloadTextures(RenderContext *ctx) {
	int textureX = ctx->screenWidth * 2;

	uploadIndexedTexture(
		&ctx->background,
		bgTexture,
		bgPalette,
		textureX,
		0,
		textureX,
		BG_HEIGHT + FONT_HEIGHT,
		BG_WIDTH,
		BG_HEIGHT,
		BG_COLOR_DEPTH
	);
	uploadIndexedTexture(
		&ctx->font,
		fontTexture,
		fontPalette,
		textureX,
		BG_HEIGHT,
		textureX + 16,
		BG_HEIGHT + FONT_HEIGHT,
		FONT_WIDTH,
		FONT_HEIGHT,
		FONT_COLOR_DEPTH
	);
}

void beginFrame(RenderContext *ctx) {
	int bufferX = (ctx->frameCounter % 2) ? ctx->screenWidth : 0;
	int bufferY = 0;

	GPUDMAChain *chain = getCurrentChain(ctx);
	chain->nextPacket  = chain->data;

	GPU_GP1 = gp1_fbOffset(bufferX, bufferY);

	uint32_t *ptr;

	ptr    = allocateGP0Packet(chain, 4);
	ptr[0] = gp0_setPage(0, true, false);
	ptr[1] = gp0_fbOffset1(bufferX, bufferY);
	ptr[2] = gp0_fbOffset2(
		bufferX + ctx->screenWidth  - 1,
		bufferY + ctx->screenHeight - 1
	);
	ptr[3] = gp0_fbOrigin(bufferX, bufferY);

#if 0
	ptr    = allocateGP0Packet(chain, 3);
	ptr[0] = gp0_rgb(0, 0, 0) | gp0_vramFill();
	ptr[1] = gp0_xy(bufferX, bufferY);
	ptr[2] = gp0_xy(ctx->screenWidth, ctx->screenHeight);
#endif
}

void endFrame(RenderContext *ctx) {
	GPUDMAChain *chain   = getCurrentChain(ctx);
	*(chain->nextPacket) = gp0_endTag(0);

	waitForGP0Ready();
	waitForVSync();
	sendGPULinkedList(chain->data);

	ctx->frameCounter++;
}

/* Drawing functions */

static bool     backgroundScrollEnabled = true;
static uint32_t backgroundScrollFrame   = 0;

void setBackgroundScrollEnabled(bool enabled) {
	backgroundScrollEnabled = enabled;
}

bool isBackgroundScrollEnabled(void) {
	return backgroundScrollEnabled;
}

void drawBackground(RenderContext *ctx) {
	GPUDMAChain       *chain = getCurrentChain(ctx);
	const TextureInfo *image = &ctx->background;

	if (backgroundScrollEnabled)
		backgroundScrollFrame++;

	int offsetX  = (backgroundScrollFrame / 2) % image->width;
	int offsetY  = (backgroundScrollFrame / 3) % (image->height * 2);
	int staggerX = image->width / 2;

	uint32_t *ptr;

	ptr    = allocateGP0Packet(chain, 1);
	ptr[0] = gp0_setPage(image->page, false, false);

	for (int y = -offsetY; y < ctx->screenHeight; y += image->height) {
		for (int x = -offsetX; x < ctx->screenWidth; x += image->width) {
			ptr    = allocateGP0Packet(chain, 4);
			ptr[0] = gp0_rectangle(true, true, false);
			ptr[1] = gp0_xy(x, y);
			ptr[2] = gp0_uv(image->u, image->v, image->clut);
			ptr[3] = gp0_xy(image->width, image->height);
		}

		offsetX +=  staggerX;
		staggerX = -staggerX;
	}
}

/* Matrix rain background */

#define RAIN_COLUMNS     8 // sparser: fewer, more widely spaced columns
#define RAIN_MAX_TRAIL  14 // upper bound; each column picks its own length
#define RAIN_MIN_TRAIL   4
#define RAIN_CELL_H     14 // vertical spacing between glyphs (glyphs ~9px tall)

// Multiply colors applied to the (white) font glyphs, head first. Kept
// deliberately dim so bright-green menu text stays clearly readable on
// top. Because glyphs are white in the texture, on-screen color is
// roughly 2x these values, clamped. Index 0 is the head; anything past a
// column's own trail length is simply not drawn.
static const uint32_t rainColors[RAIN_MAX_TRAIL] = {
	0x123E16, // head - brightest (dimmed down)
	0x00330F,
	0x00290C,
	0x00210A,
	0x001B08,
	0x001606,
	0x001205,
	0x000E04,
	0x000B03,
	0x000903,
	0x000702,
	0x000502,
	0x000301,
	0x000200  // tail - nearly black
};

// Per-column state, persisted across frames; initialized on first call.
static bool    rainInit = false;
static int16_t rainHeadY[RAIN_COLUMNS];
static uint8_t rainSpeed[RAIN_COLUMNS]; // px/frame
static uint8_t rainTrail[RAIN_COLUMNS]; // this column's trail length

static uint32_t rainRngState = 0x9e3779b9u;

static inline uint32_t rainRand(void) {
	rainRngState ^= rainRngState << 13;
	rainRngState ^= rainRngState >> 17;
	rainRngState ^= rainRngState << 5;
	return rainRngState;
}

// Deterministic per-cell glyph: stable for a given (column, screen row)
// so characters don't strobe every frame - only the brightness sweep
// moves as the head descends.
static inline char rainGlyph(int column, int row) {
	uint32_t h = (uint32_t) (column * 2654435761u) ^ (uint32_t) (row * 40503u);
	h ^= h >> 15;

	// Printable ASCII range '!'(0x21) .. '~'(0x7e).
	return (char) (0x21 + (h % (0x7e - 0x21 + 1)));
}

static void rainRespawn(int c, int screenH, bool startAbove) {
	// Stagger start heights so columns don't march in lockstep.
	if (startAbove)
		rainHeadY[c] = -(int16_t) (rainRand() % (screenH / 2 + RAIN_CELL_H));
	else
		rainHeadY[c] = -(int16_t) (rainRand() % (screenH + RAIN_CELL_H));

	rainSpeed[c] = 1 + (rainRand() % 4); // 1..4 px/frame (more variety)
	rainTrail[c] =
		RAIN_MIN_TRAIL + (rainRand() % (RAIN_MAX_TRAIL - RAIN_MIN_TRAIL + 1));
}

void drawMatrixRain(RenderContext *ctx) {
	int screenW = ctx->screenWidth;
	int screenH = ctx->screenHeight;
	int spacing = screenW / RAIN_COLUMNS;

	if (!rainInit) {
		for (int c = 0; c < RAIN_COLUMNS; c++)
			rainRespawn(c, screenH, false);
		rainInit = true;
	}

	// Clear the whole framebuffer to solid black FIRST. The rain only
	// draws sparse glyphs, so without this the previous frame's pixels
	// (highlight bar, old glyphs) would smear and never go away.
	drawRect(ctx, 0, 0, screenW, screenH, 0x000000, false);

	// The existing "Background scroll" toggle (R2) also freezes the rain.
	if (backgroundScrollEnabled) {
		for (int c = 0; c < RAIN_COLUMNS; c++) {
			rainHeadY[c] += rainSpeed[c];

			// Once the whole trail has fallen off the bottom, respawn the
			// column above the top with a fresh length/speed.
			if (rainHeadY[c] - rainTrail[c] * RAIN_CELL_H > screenH)
				rainRespawn(c, screenH, true);
		}
	}

	setFontPage(ctx);

	for (int c = 0; c < RAIN_COLUMNS; c++) {
		int x     = c * spacing + (spacing / 2) - 3;
		int trail = rainTrail[c];

		for (int i = 0; i < trail; i++) {
			int y = rainHeadY[c] - i * RAIN_CELL_H;

			if ((y < -RAIN_CELL_H) || (y > screenH))
				continue;

			int row = y / RAIN_CELL_H;

			drawChar(ctx, x, y, rainColors[i], rainGlyph(c, row));
		}
	}
}

void drawRect(
	RenderContext *ctx,
	int           x,
	int           y,
	int           width,
	int           height,
	uint32_t      color,
	bool          blend
) {
	GPUDMAChain *chain = getCurrentChain(ctx);

	uint32_t *ptr;

	ptr    = allocateGP0Packet(chain, 3);
	ptr[0] = color | gp0_rectangle(false, false, blend);
	ptr[1] = gp0_xy(x, y);
	ptr[2] = gp0_xy(width, height);
}

void drawGradientRectH(
	RenderContext *ctx,
	int           x,
	int           y,
	int           width,
	int           height,
	uint32_t      leftColor,
	uint32_t      rightColor,
	bool          blend
) {
	GPUDMAChain *chain = getCurrentChain(ctx);

	uint32_t *ptr;

	ptr    = allocateGP0Packet(chain, 9);
	ptr[0] = gp0_setPage(0, true, false);
	ptr[1] = leftColor | gp0_shadedQuad(true, false, blend);
	ptr[2] = gp0_xy(x, y);
	ptr[3] = rightColor;
	ptr[4] = gp0_xy(x + width, y);
	ptr[5] = leftColor;
	ptr[6] = gp0_xy(x, y + height);
	ptr[7] = rightColor;
	ptr[8] = gp0_xy(x + width, y + height);
}
