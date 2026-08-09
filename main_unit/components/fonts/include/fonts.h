#pragma once

/*
 * Tiny embedded bitmap fonts for the Smart Keyboard UI.
 *
 * We don't depend on LVGL, so the font format is deliberately
 * minimal: one byte per row, MSB = leftmost pixel (the 8x8 base
 * font packs each row LSB-first instead; see font_pixel_8x8).
 * The 8x8 base font is the public-domain font8x8 (dhepper), kept
 * because it stays legible when condensed to sub-8px key labels.
 * The larger UI tables are derived from Greybeard, a vector /
 * bitmap port of Uwe Waldmann's UW ttyp0 (MIT License):
 * https://github.com/flowchartsman/greybeard
 *   - the 10x20 font is rendered from Greybeard gb-18,
 *   - the 12x16 font is rendered from Greybeard gb-16.
 * Larger pixel sizes are produced by integer-scaling each glyph
 * at draw time -- the source bitmap is the same.
 *
 * The display component is responsible for actually plotting
 * pixels; this file just hands out raw glyph rows.
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FONT_BASE_W 8
#define FONT_BASE_H 8

/* Native dimensions of the higher-resolution font used for
 * single-glyph key labels (X11 misc-fixed 10x20, public domain). */
#define FONT10X20_W 10
#define FONT10X20_H 20

/* Native dimensions of the smaller UI font used for single-glyph
 * key labels on low-resolution 320x240 panels, where the 10x20
 * glyph does not fit a key cell. Both ASCII and the Cyrillic
 * glyphs are provided (see font_glyph_12x16_cp), so Latin and
 * Cyrillic single-letter labels render at the same crisp native
 * size on those panels. */
#define FONT12X16_W 12
#define FONT12X16_H 16

/* Returns the 8 row bytes for ASCII character c, or the glyph for
 * '?' if c is outside 0x20..0x7E. The returned pointer is valid
 * for the lifetime of the process. */
const uint8_t *font_glyph_8x8(char c);

/* Returns the 40 raw bytes (20 rows of 2 bytes, MSB = leftmost
 * pixel, bit 7 of byte0 = col 0, bit 6 of byte1 = col 9) for the
 * 10x20 glyph of ASCII character c, or '?' if out of range. The
 * returned pointer is valid for the lifetime of the process. */
const uint8_t *font_glyph_10x20(char c);

/* Like font_glyph_10x20() but keyed by Unicode codepoint, so it
 * can also return the embedded Cyrillic glyphs (Ukrainian alphabet,
 * upper + lower case). ASCII codepoints (0x20..0x7E) fall through
 * to the base table; unknown codepoints return the '?' glyph. */
const uint8_t *font_glyph_10x20_cp(uint32_t cp);

/* Returns the 32 raw bytes (16 rows of 2 bytes, MSB = leftmost
 * pixel, bit 7 of byte0 = col 0, bit 4 of byte1 = col 11) for the
 * 12x16 glyph of the given Unicode codepoint. ASCII codepoints
 * (0x20..0x7E) fall through to the base table; the embedded
 * Cyrillic glyphs (Ukrainian alphabet, upper + lower case) are
 * also covered; any other codepoint returns the '?' glyph. Used on
 * low-resolution panels as a crisper alternative to downscaling
 * the 10x20 glyphs. The returned pointer is valid for the lifetime
 * of the process. */
const uint8_t *font_glyph_12x16_cp(uint32_t cp);

/* True if bit (col, row) of the given 32-byte 12x16 glyph is set.
 * col in [0, 11], row in [0, 15]. Use with font_glyph_12x16_cp()
 * to avoid re-resolving the glyph per pixel. */
static inline bool font_pixel_in_12x16(const uint8_t *g, int col, int row)
{
    if (!g || col < 0 || col >= FONT12X16_W ||
        row < 0 || row >= FONT12X16_H) {
        return false;
    }
    uint16_t row_bits = ((uint16_t)g[row * 2] << 8) | g[row * 2 + 1];
    return (row_bits >> (15 - col)) & 1;
}

/* True if bit (col, row) of the given 40-byte 10x20 glyph is set.
 * col in [0, 9], row in [0, 19]. Use with font_glyph_10x20_cp()
 * to avoid re-resolving the glyph per pixel. */
static inline bool font_pixel_in_10x20(const uint8_t *g, int col, int row)
{
    if (!g || col < 0 || col >= FONT10X20_W ||
        row < 0 || row >= FONT10X20_H) {
        return false;
    }
    uint16_t row_bits = ((uint16_t)g[row * 2] << 8) | g[row * 2 + 1];
    return (row_bits >> (15 - col)) & 1;
}

/* True if bit (col, row) of the 10x20 glyph for c is set.
 * col in [0, 9], row in [0, 19]. */
static inline bool font_pixel_10x20(char c, int col, int row)
{
    if (col < 0 || col >= FONT10X20_W ||
        row < 0 || row >= FONT10X20_H) {
        return false;
    }
    const uint8_t *g = font_glyph_10x20(c);
    /* Stored MSB-first across the two-byte row; col 0 = bit 7 of
     * byte 0, col 8 = bit 7 of byte 1, col 9 = bit 6 of byte 1. */
    uint16_t row_bits = ((uint16_t)g[row * 2] << 8) | g[row * 2 + 1];
    return (row_bits >> (15 - col)) & 1;
}

/* True if bit (col, row) of the 8x8 glyph for c is set.
 * col in [0,7], row in [0,7]. */
static inline bool font_pixel_8x8(char c, int col, int row)
{
    const uint8_t *g = font_glyph_8x8(c);
    /* font8x8_basic stores each row LSB-first (bit 0 = leftmost). */
    return (g[row] >> col) & 1;
}

/* True if pixel (px, py) of the glyph for c, scaled to scale*8 px
 * square, is set. scale in [1, 8]. */
static inline bool font_pixel_scaled(char c, int scale, int px, int py)
{
    if (scale < 1) scale = 1;
    int col = px / scale;
    int row = py / scale;
    if (col < 0 || col >= FONT_BASE_W || row < 0 || row >= FONT_BASE_H) {
        return false;
    }
    return font_pixel_8x8(c, col, row);
}

#ifdef __cplusplus
}
#endif
