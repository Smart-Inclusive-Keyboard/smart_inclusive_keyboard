/*
 * Display front-end: pixel-setting / fill_rect / draw_string
 * that operate on a flat RGB565 framebuffer allocated by the
 * per-backend init code. The backend (display_ili9341.c)
 * registers its framebuffer pointer + flush function via
 * display_register_backend(); this file only knows how to
 * draw into a generic RGB565 buffer.
 *
 * Sliced this way so future panel backends (ST7789, MIPI-DSI,
 * e-paper) plug in with a one-function vtable and don't have
 * to re-implement the glyph rasteriser.
 */

#include "display.h"

#include <string.h>

#include <esp_log.h>

#include "board.h"
#include "fonts.h"

static const char *TAG = "display";

/* Backend vtable filled in by display_ili9341_init() (or any
 * future backend init). */
typedef struct {
    uint16_t *fb;
    int       w;
    int       h;
    void    (*flush)(int x0, int y0, int x1, int y1);
    void    (*set_backlight)(int percent);
} display_backend_t;

static display_backend_t s_be;

/* Dirty bbox in framebuffer coordinates (inclusive). When equal
 * to {0, 0, -1, -1} the bbox is empty. */
static int s_dx0, s_dy0, s_dx1, s_dy1;

static void bbox_reset(void)
{
    s_dx0 = s_dy0 = 0;
    s_dx1 = s_dy1 = -1;
}

static void bbox_expand(int x0, int y0, int x1, int y1)
{
    if (s_dx1 < s_dx0) {
        s_dx0 = x0; s_dy0 = y0; s_dx1 = x1; s_dy1 = y1;
        return;
    }
    if (x0 < s_dx0) s_dx0 = x0;
    if (y0 < s_dy0) s_dy0 = y0;
    if (x1 > s_dx1) s_dx1 = x1;
    if (y1 > s_dy1) s_dy1 = y1;
}

/* Backend hookup. Called from display_ili9341.c after the
 * panel + framebuffer are ready. */
void display_register_backend(uint16_t *fb, int w, int h,
                              void (*flush)(int, int, int, int),
                              void (*set_backlight)(int))
{
    s_be.fb = fb;
    s_be.w  = w;
    s_be.h  = h;
    s_be.flush = flush;
    s_be.set_backlight = set_backlight;
    bbox_reset();
}

/* ----- Backend selection ----- */

void display_ili9341_init(void);    /* implemented in display_ili9341.c */

void display_init(void)
{
    const board_t *b = board_get();

    switch (b->display_type) {
    case BOARD_DISPLAY_ILI9341:
#if CONFIG_BOARD_HAS_DISPLAY_ILI9341
        display_ili9341_init();
#else
        ESP_LOGE(TAG, "ILI9341 selected but driver not built in");
#endif
        break;
    case BOARD_DISPLAY_NONE:
    default:
        ESP_LOGW(TAG, "No display configured for this board");
        break;
    }
}

int display_width(void)  { return s_be.w; }
int display_height(void) { return s_be.h; }

void display_clear(uint16_t color)
{
    if (!s_be.fb) return;
    int n = s_be.w * s_be.h;
    /* fill 16-bit color into the framebuffer. */
    for (int i = 0; i < n; ++i) s_be.fb[i] = color;
    bbox_expand(0, 0, s_be.w - 1, s_be.h - 1);
}

void display_set_pixel(int x, int y, uint16_t color)
{
    if (!s_be.fb) return;
    if (x < 0 || y < 0 || x >= s_be.w || y >= s_be.h) return;
    s_be.fb[y * s_be.w + x] = color;
    bbox_expand(x, y, x, y);
}

void display_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (!s_be.fb || w <= 0 || h <= 0) return;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w; if (x1 > s_be.w) x1 = s_be.w;
    int y1 = y + h; if (y1 > s_be.h) y1 = s_be.h;
    if (x0 >= x1 || y0 >= y1) return;
    for (int yy = y0; yy < y1; ++yy) {
        uint16_t *row = &s_be.fb[yy * s_be.w + x0];
        for (int xx = 0; xx < x1 - x0; ++xx) row[xx] = color;
    }
    bbox_expand(x0, y0, x1 - 1, y1 - 1);
}

void display_draw_char(int x, int y, char c, int scale,
                       uint16_t fg, uint16_t bg, bool transparent)
{
    if (scale < 1) scale = 1;
    int dim = FONT_BASE_W * scale;
    for (int py = 0; py < dim; ++py) {
        int row = py / scale;
        for (int px = 0; px < dim; ++px) {
            int col = px / scale;
            if (font_pixel_8x8(c, col, row)) {
                display_set_pixel(x + px, y + py, fg);
            } else if (!transparent) {
                display_set_pixel(x + px, y + py, bg);
            }
        }
    }
}

void display_draw_string(int x, int y, const char *s, int scale,
                         uint16_t fg, uint16_t bg, bool transparent)
{
    if (!s) return;
    int adv = FONT_BASE_W * (scale < 1 ? 1 : scale);
    for (int i = 0; s[i] != '\0'; ++i) {
        display_draw_char(x + i * adv, y, s[i], scale, fg, bg, transparent);
    }
}

void display_draw_char_wh(int x, int y, char c, int cw, int ch,
                          uint16_t fg, uint16_t bg, bool transparent)
{
    if (cw < 1) cw = 1;
    if (ch < 1) ch = 1;
    for (int py = 0; py < ch; ++py) {
        /* Nearest-neighbour map the target row back onto one of the
         * 8 source rows; the same for columns below. */
        int row = py * FONT_BASE_H / ch;
        for (int px = 0; px < cw; ++px) {
            int col = px * FONT_BASE_W / cw;
            if (font_pixel_8x8(c, col, row)) {
                display_set_pixel(x + px, y + py, fg);
            } else if (!transparent) {
                display_set_pixel(x + px, y + py, bg);
            }
        }
    }
}

void display_draw_string_wh(int x, int y, const char *s, int cw, int ch,
                            uint16_t fg, uint16_t bg, bool transparent)
{
    if (!s) return;
    for (int i = 0; s[i] != '\0'; ++i) {
        display_draw_char_wh(x + i * cw, y, s[i], cw, ch,
                             fg, bg, transparent);
    }
}

void display_draw_char_10x20(int x, int y, char c,
                             uint16_t fg, uint16_t bg, bool transparent)
{
    for (int py = 0; py < FONT10X20_H; ++py) {
        for (int px = 0; px < FONT10X20_W; ++px) {
            if (font_pixel_10x20(c, px, py)) {
                display_set_pixel(x + px, y + py, fg);
            } else if (!transparent) {
                display_set_pixel(x + px, y + py, bg);
            }
        }
    }
}

void display_draw_string_10x20(int x, int y, const char *s,
                               uint16_t fg, uint16_t bg, bool transparent)
{
    if (!s) return;
    for (int i = 0; s[i] != '\0'; ++i) {
        display_draw_char_10x20(x + i * FONT10X20_W, y, s[i],
                                fg, bg, transparent);
    }
}

void display_draw_glyph_10x20_cp(int x, int y, uint32_t cp,
                                 uint16_t fg, uint16_t bg, bool transparent)
{
    const uint8_t *g = font_glyph_10x20_cp(cp);
    for (int py = 0; py < FONT10X20_H; ++py) {
        for (int px = 0; px < FONT10X20_W; ++px) {
            if (font_pixel_in_10x20(g, px, py)) {
                display_set_pixel(x + px, y + py, fg);
            } else if (!transparent) {
                display_set_pixel(x + px, y + py, bg);
            }
        }
    }
}

void display_draw_glyph_10x20_cp_wh(int x, int y, uint32_t cp,
                                    int cw, int ch,
                                    uint16_t fg, uint16_t bg,
                                    bool transparent)
{
    if (cw < 1) cw = 1;
    if (ch < 1) ch = 1;
    const uint8_t *g = font_glyph_10x20_cp(cp);
    for (int py = 0; py < ch; ++py) {
        /* Nearest-neighbour map the target row/col back onto the
         * native 10x20 source grid. */
        int row = py * FONT10X20_H / ch;
        for (int px = 0; px < cw; ++px) {
            int col = px * FONT10X20_W / cw;
            if (font_pixel_in_10x20(g, col, row)) {
                display_set_pixel(x + px, y + py, fg);
            } else if (!transparent) {
                display_set_pixel(x + px, y + py, bg);
            }
        }
    }
}

void display_draw_glyph_12x16_cp(int x, int y, uint32_t cp,
                                 uint16_t fg, uint16_t bg, bool transparent)
{
    const uint8_t *g = font_glyph_12x16_cp(cp);
    for (int py = 0; py < FONT12X16_H; ++py) {
        for (int px = 0; px < FONT12X16_W; ++px) {
            if (font_pixel_in_12x16(g, px, py)) {
                display_set_pixel(x + px, y + py, fg);
            } else if (!transparent) {
                display_set_pixel(x + px, y + py, bg);
            }
        }
    }
}

void display_draw_glyph_12x16_cp_wh(int x, int y, uint32_t cp,
                                    int cw, int ch,
                                    uint16_t fg, uint16_t bg,
                                    bool transparent)
{
    if (cw < 1) cw = 1;
    if (ch < 1) ch = 1;
    const uint8_t *g = font_glyph_12x16_cp(cp);
    for (int py = 0; py < ch; ++py) {
        /* Nearest-neighbour map the target row/col back onto the
         * native 12x16 source grid. */
        int row = py * FONT12X16_H / ch;
        for (int px = 0; px < cw; ++px) {
            int col = px * FONT12X16_W / cw;
            if (font_pixel_in_12x16(g, col, row)) {
                display_set_pixel(x + px, y + py, fg);
            } else if (!transparent) {
                display_set_pixel(x + px, y + py, bg);
            }
        }
    }
}

void display_flush(void)
{
    if (!s_be.flush) return;
    if (s_dx1 < s_dx0 || s_dy1 < s_dy0) return;  /* empty bbox */
    s_be.flush(s_dx0, s_dy0, s_dx1, s_dy1);
    bbox_reset();
}

void display_set_backlight(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    if (s_be.set_backlight) s_be.set_backlight(percent);
}
