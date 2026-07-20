/*
 * ps1-bare-metal - (C) 2023-2025 spicyjpeg
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

#pragma once

#include <stdint.h>
#include "common/gpu.h"
#include "main/renderer.h"

#define FONT_FIRST_TABLE_CHAR '!'
#define FONT_SPACE_WIDTH       4
#define FONT_TAB_WIDTH        32
#define FONT_LINE_HEIGHT      10

#ifdef __cplusplus
extern "C" {
#endif

void printString(
	RenderContext *ctx,
	int           x,
	int           y,
	uint32_t      color,
	const char    *str
);

// Lower-level glyph drawing, used when many individual characters need to
// be drawn at scattered positions (e.g. the Matrix rain background)
// without paying for a texture-page reselect on every single one.
// Call setFontPage() once, then drawChar() as many times as needed with
// no other page-changing draw calls in between.
void setFontPage(RenderContext *ctx);
void drawChar(RenderContext *ctx, int x, int y, uint32_t color, char c);
int getStringWidth(const char *str);

#ifdef __cplusplus
}
#endif
