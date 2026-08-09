#pragma once

/*
 * Minimal framebuffer-backed display API for the Smart Keyboard UI.
 *
 * Sits on top of the panel driver (per-board file under src/)
 * which is responsible for:
 *   - bringing up the controller (QSPI bus, init sequence, backlight)
 *   - allocating a width*height RGB565 framebuffer in PSRAM
 *   - exposing a flush hook that DMAs the dirty bbox to the panel
 *
 * The UI layer (keyboard_ui) talks only to this header. There is
 * no LVGL involved.
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Bring up the panel driver for the compiled-in board. Allocates
 * the framebuffer in PSRAM. Aborts the boot with a fatal log on
 * boards whose display backend is not implemented yet.
 *
 * Must be called after board_init() and from a task with at
 * least ~4 KB of stack (display backends may queue DMA
 * descriptors and short-block on the SPI peripheral semaphore).
 */
void display_init(void);

/* Pixel dimensions of the logical framebuffer (after rotation). */
int display_width(void);
int display_height(void);

/* Set every pixel of the framebuffer to one RGB565 color. The
 * change is not visible until display_flush(). */
void display_clear(uint16_t color565);

/* Plot a single pixel. Out-of-bounds coordinates are silently
 * dropped. */
void display_set_pixel(int x, int y, uint16_t color565);

/* Fill an axis-aligned rectangle. Clipped to the screen bounds. */
void display_fill_rect(int x, int y, int w, int h, uint16_t color565);

/* Draw an 8x8 ASCII glyph from the embedded font at (x, y),
 * integer-scaled by `scale` (1 = native, 2 = 16x16 pixels, ...).
 * `fg` pixels are drawn; `bg` pixels are skipped if `transparent`,
 * otherwise filled with the bg color. */
void display_draw_char(int x, int y, char c, int scale,
                       uint16_t fg, uint16_t bg, bool transparent);

/* Draw a null-terminated ASCII string at (x, y) advancing by
 * 8*scale pixels per character. */
void display_draw_string(int x, int y, const char *s, int scale,
                         uint16_t fg, uint16_t bg, bool transparent);

/* Draw an 8x8 ASCII glyph nearest-neighbour scaled to an arbitrary
 * width x height box (in pixels). Unlike display_draw_char(), which
 * only supports integer up-scaling, this can also SHRINK the glyph
 * below its native 8x8 size, which is used to squeeze multi-letter
 * key labels (Esc, Caps, Bksp, ...) into small cells on low-resolution
 * panels. `cw` / `ch` must be >= 1. */
void display_draw_char_wh(int x, int y, char c, int cw, int ch,
                          uint16_t fg, uint16_t bg, bool transparent);

/* Draw a null-terminated ASCII string whose glyphs are each rendered
 * `cw` pixels wide and `ch` pixels tall (nearest-neighbour scaled from
 * the 8x8 font), advancing by `cw` pixels per character. Lets a caller
 * fit a short label into a fixed pixel width regardless of its length. */
void display_draw_string_wh(int x, int y, const char *s, int cw, int ch,
                            uint16_t fg, uint16_t bg, bool transparent);

/* Higher-resolution 10x20 variants for single-letter key labels.
 * Glyphs are drawn at their native 10x20 size (no scaling); the
 * extra source-pixel density gives noticeably smoother edges than
 * the integer-scaled 8x8 font at comparable on-screen sizes. */
void display_draw_char_10x20(int x, int y, char c,
                             uint16_t fg, uint16_t bg, bool transparent);
void display_draw_string_10x20(int x, int y, const char *s,
                               uint16_t fg, uint16_t bg, bool transparent);

/* Draw a single 10x20 glyph identified by Unicode codepoint. Used
 * for non-ASCII single-letter key labels (e.g. the Cyrillic glyphs
 * of the Ukrainian layout). ASCII codepoints render the same glyph
 * as display_draw_char_10x20(); unknown codepoints render '?'. */
void display_draw_glyph_10x20_cp(int x, int y, uint32_t cp,
                                 uint16_t fg, uint16_t bg, bool transparent);

/* Draw a 10x20 codepoint glyph nearest-neighbour scaled to an
 * arbitrary width x height box (in pixels). Like
 * display_draw_glyph_10x20_cp() but can shrink the glyph below its
 * native 10x20 size, used to fit non-ASCII single-letter key labels
 * (e.g. the Cyrillic glyphs of the Ukrainian layout) into small
 * cells on low-resolution panels. `cw` / `ch` must be >= 1. */
void display_draw_glyph_10x20_cp_wh(int x, int y, uint32_t cp,
                                    int cw, int ch,
                                    uint16_t fg, uint16_t bg,
                                    bool transparent);

/* Draw a single 12x16 glyph identified by Unicode codepoint. This
 * smaller UI font carries only the Cyrillic glyphs and is used on
 * low-resolution 320x240 panels, where the 10x20 glyph does not fit
 * a key cell and downscaling it looks poor. Unknown codepoints
 * render '?'. */
void display_draw_glyph_12x16_cp(int x, int y, uint32_t cp,
                                 uint16_t fg, uint16_t bg, bool transparent);

/* Draw a 12x16 codepoint glyph nearest-neighbour scaled to an
 * arbitrary width x height box (in pixels), for cells too small even
 * for the native 12x16 glyph. `cw` / `ch` must be >= 1. */
void display_draw_glyph_12x16_cp_wh(int x, int y, uint32_t cp,
                                    int cw, int ch,
                                    uint16_t fg, uint16_t bg,
                                    bool transparent);

/* Push the framebuffer's dirty bounding box to the panel. The
 * backend internally tracks the bbox; pixel-setting calls expand
 * it. Returns when DMA completes. */
void display_flush(void);

/* Backlight 0..100 (clamped). No-op when the board has no
 * controllable backlight. */
void display_set_backlight(int percent);

#ifdef __cplusplus
}
#endif
