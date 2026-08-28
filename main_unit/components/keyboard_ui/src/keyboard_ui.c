/*
 * Virtual-keyboard renderer and selection state machine.
 *
 * Layout
 * ------
 *   +----------------------------------------------------------+
 *   | status bar : layout | mods | HID | mode                  |
 *   +----------------------------------------------------------+
 *   | key grid: rows x cols, selected cell highlighted         |
 *   +----------------------------------------------------------+
 *
 * The key grid uses square cells sized to fit both axes
 * (cell = min(width/cols, avail_h/rows)). Any leftover space
 * after laying out the cells becomes equal margins on the
 * sides -- typically vertical, since the panel is wider than
 * tall. On the larger 480x320 panels, single-character labels
 * render with the higher-density 10x20 font; on smaller 320x240
 * panels and for multi-character labels (F1..F12, Caps, Bksp,
 * Esc, ...) they fall back to integer-scaled 8x8 glyphs.
 *
 * Drawing happens on a FreeRTOS task driven by a redraw queue
 * so the input handlers can fire keyboard_ui_request_redraw()
 * cheaply from any context.
 */

#include "keyboard_ui.h"

#include <stdio.h>
#include <string.h>

#include <esp_log.h>
#include <esp_timer.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#include "display.h"
#include "theme.h"
#include "fonts.h"
#include "kb_layout.h"
#include "hid.h"
#include "narrator.h"
#include "audio.h"

static const char *TAG = "kb_ui";

#define STATUS_BAR_H 14
#define NVS_NAMESPACE "sk_ui"

/* Two Win-key toggles within this window send a standalone Win
 * key-down/key-up HID report, mimicking a physical keyboard's
 * "tap Win to open the Start menu" gesture. */
#define WIN_DOUBLE_TAP_MS 500

typedef struct {
    int  sel_row;
    int  sel_col;
    uint8_t  mod_sticky;    /* persistent across keypresses     */
    uint8_t  mod_oneshot;   /* cleared after the next keypress  */
    bool     hid_connected;
    uint32_t passkey;       /* 0 = none, else 6-digit value     */
    keyboard_ui_mode_t mode;
    int      menu_sel;      /* selected row in the settings menu */
    int      mouse_speed;   /* 0..KB_MOUSE_SPEED_LEVELS-1        */
    bool     nav_rollover;  /* wrap selection at the grid edges  */
    uint32_t win_last_press_ms; /* for Win-key double-tap detection */
    char     hid_status[32];
} ui_state_t;

static ui_state_t s_st;
static QueueHandle_t s_redraw_q;

/* Mouse-pointer speed levels: maximum per-poll pixel delta at
 * full axis deflection, from slow to fast. The user picks one of
 * these in the settings menu; input_router scales the live analog
 * axis value by the selected entry. */
#define MOUSE_SPEED_DEFAULT 3   /* index into s_mouse_speed_step  */
static const int s_mouse_speed_step[KB_MOUSE_SPEED_LEVELS] = {
    3, 6, 10, 16, 24, 34, 48,
};

/* ----- NVS persistence ----- */

static void nvs_save_strings(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    /* The active language is intentionally not persisted; the device
     * always boots with the first available layout. */
    nvs_set_str(h, "theme",  theme_active()->name);
    nvs_set_u8(h, "mousespd", (uint8_t)s_st.mouse_speed);
    nvs_set_u8(h, "navroll", (uint8_t)(s_st.nav_rollover ? 1 : 0));
#if CONFIG_BOARD_HAS_SPEAKER
    nvs_set_u8(h, "soundvol", (uint8_t)audio_get_volume());
#endif
    nvs_commit(h);
    nvs_close(h);
}

static void nvs_load_strings(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return;
    char buf[16];
    size_t len = sizeof(buf);
    if (nvs_get_str(h, "theme", buf, &len) == ESP_OK) {
        theme_set_active_by_name(buf);
    }
    uint8_t spd;
    if (nvs_get_u8(h, "mousespd", &spd) == ESP_OK && spd < KB_MOUSE_SPEED_LEVELS) {
        s_st.mouse_speed = spd;
    }
    uint8_t roll;
    if (nvs_get_u8(h, "navroll", &roll) == ESP_OK) {
        s_st.nav_rollover = (roll != 0);
    }
#if CONFIG_BOARD_HAS_SPEAKER
    uint8_t vol;
    if (nvs_get_u8(h, "soundvol", &vol) == ESP_OK  && vol <= 100) {
        audio_set_volume(vol);
    }
#endif
    nvs_close(h);
}

/* ----- Drawing ----- */

static void draw_status_bar(const theme_t *th)
{
    int w = display_width();
    display_fill_rect(0, 0, w, STATUS_BAR_H, th->status_bar_bg);

    char line[80];
    /* Left side: layout name + modifier indicators. */
    snprintf(line, sizeof(line), "[%s]", kb_layout_active()->name);
    display_draw_string(2, 3, line, 1, th->status_ind_fg,
                        th->status_bar_bg, true);

    int x = 2 + (int)strlen(line) * 8 + 8;
    const struct { const char *t; uint8_t m; } mods[] = {
        { "Sh", HID_MOD_LSHIFT },
        { "Ct", HID_MOD_LCTRL  },
        { "Al", HID_MOD_LALT   },
        { "AG", HID_MOD_RALT   },
        { "Wn", HID_MOD_LGUI   },
    };
    for (size_t i = 0; i < sizeof(mods) / sizeof(mods[0]); ++i) {
        bool on = (s_st.mod_sticky | s_st.mod_oneshot) & mods[i].m;
        uint16_t bg = on ? th->status_ind_active_bg : th->status_bar_bg;
        uint16_t fg = on ? th->status_ind_active_fg : th->status_ind_fg;
        display_fill_rect(x - 2, 1, 2 * 8 + 4, STATUS_BAR_H - 2, bg);
        display_draw_string(x, 3, mods[i].t, 1, fg, bg, false);
        x += 2 * 8 + 8;
    }

    /* Right side: HID link indicator + status text. */
    uint16_t hid_col = s_st.hid_connected ? th->conn_connected
                                          : th->conn_disconnected;
    display_fill_rect(w - 8, 3, 6, STATUS_BAR_H - 6, hid_col);

    if (s_st.passkey) {
        char pk[16];
        snprintf(pk, sizeof(pk), "PIN %06lu",
                 (unsigned long)s_st.passkey);
        int len = (int)strlen(pk);
        display_draw_string(w - 16 - len * 8, 3, pk, 1,
                            th->status_ind_fg, th->status_bar_bg, true);
    } else if (s_st.hid_status[0]) {
        int len = (int)strlen(s_st.hid_status);
        if (len > 16) len = 16;
        display_draw_string(w - 16 - len * 8, 3, s_st.hid_status, 1,
                            th->status_ind_fg, th->status_bar_bg, true);
    }
}

/* ----- Icon glyphs ----- */
/*
 * Symbolic icons drawn directly via display_set_pixel so they
 * don't need extra entries in the ASCII font table. Each helper
 * fills the supplied (x, y, w, h) bbox with `bg`, then paints the
 * symbol in `fg`. The bbox is the inner padded area of the key
 * cell (a few pixels of margin removed by the caller).
 */

static void icon_fill_triangle(int x, int y, int w, int h,
                               uint16_t fg, char dir)
{
    /* Solid isoceles triangle pointing in the given direction
     * ('U' up, 'D' down, 'L' left, 'R' right), inscribed in the
     * (w x h) bbox. */
    if (dir == 'U' || dir == 'D') {
        for (int row = 0; row < h; ++row) {
            int frac = (dir == 'U') ? row : (h - 1 - row);
            int half = (frac * (w / 2)) / (h > 1 ? h - 1 : 1);
            int x0 = x + w / 2 - half;
            int x1 = x + w / 2 + half;
            for (int px = x0; px <= x1; ++px) {
                display_set_pixel(px, y + row, fg);
            }
        }
    } else {
        for (int col = 0; col < w; ++col) {
            int frac = (dir == 'L') ? col : (w - 1 - col);
            int half = (frac * (h / 2)) / (w > 1 ? w - 1 : 1);
            int y0 = y + h / 2 - half;
            int y1 = y + h / 2 + half;
            for (int py = y0; py <= y1; ++py) {
                display_set_pixel(x + col, py, fg);
            }
        }
    }
}

static void icon_space(int x, int y, int w, int h, uint16_t fg)
{
    /* Horizontal bar with short downward tabs at both ends -- the
     * conventional space-bar glyph used by IDEs / editors. */
    int bar_h = h / 6; if (bar_h < 1) bar_h = 1;
    int tab_h = h / 4; if (tab_h < 2) tab_h = 2;
    int margin = w / 8;
    int x0 = x + margin;
    int x1 = x + w - margin - 1;
    int by = y + h - 1 - bar_h - 1;
    /* horizontal bar */
    display_fill_rect(x0, by, x1 - x0 + 1, bar_h, fg);
    /* left + right tabs */
    display_fill_rect(x0, by - tab_h + 1, bar_h, tab_h, fg);
    display_fill_rect(x1 - bar_h + 1, by - tab_h + 1, bar_h, tab_h, fg);
}

static void icon_backspace(int x, int y, int w, int h, uint16_t fg)
{
    /* Left-pointing arrow with a horizontal stem -- the
     * conventional Backspace glyph. Arrowhead is an isoceles
     * triangle on the left; stem is a horizontal bar extending
     * to the right edge. */
    int stroke = h / 8; if (stroke < 1) stroke = 1;
    int head_h = (h * 2) / 3; if (head_h < 4) head_h = h;
    int head_w = head_h / 2 + 1;
    int mid_y = y + h / 2;
    int hy = mid_y - head_h / 2;
    int hx = x + 1;
    /* Arrowhead triangle pointing left. */
    icon_fill_triangle(hx, hy, head_w, head_h, fg, 'L');
    /* Stem from arrowhead base to the right edge, vertically centred. */
    int stem_x0 = hx + head_w;
    int stem_x1 = x + w - 1;
    if (stem_x1 > stem_x0) {
        display_fill_rect(stem_x0, mid_y - stroke / 2,
                          stem_x1 - stem_x0 + 1, stroke, fg);
    }
}

static void icon_enter(int x, int y, int w, int h, uint16_t fg)
{
    /* Return-arrow: a horizontal stroke at mid-height running from
     * the right edge leftward, with a vertical drop to the right
     * end, plus an arrowhead at the left tip. */
    int stroke = h / 8; if (stroke < 1) stroke = 1;
    int margin_x = w / 6;
    int margin_y = h / 4;
    int top = y + margin_y;
    int mid_y = y + h - margin_y - 1;
    int left_x = x + margin_x;
    int right_x = x + w - margin_x - 1;
    /* horizontal */
    display_fill_rect(left_x, mid_y - stroke + 1,
                      right_x - left_x + 1, stroke, fg);
    /* vertical drop on the right */
    display_fill_rect(right_x - stroke + 1, top,
                      stroke, mid_y - top + 1, fg);
    /* arrowhead at the left tip */
    int head = h / 4; if (head < 2) head = 2;
    icon_fill_triangle(left_x - head + 1, mid_y - head + 1,
                       head, head * 2 - 1, fg, 'L');
}

/* True if this key should be drawn as an icon, not text. */
static bool key_uses_icon(const kb_key_t *k)
{
    if (!k) return false;
    if (k->special == KB_KEY_SPECIAL_SPACE) return true;
    if (k->special == KB_KEY_SPECIAL_ENTER) return true;
    if (k->special == KB_KEY_SPECIAL_BACKSPACE) return true;
    switch (k->hid_usage) {
    case HID_USAGE_UP: case HID_USAGE_DOWN:
    case HID_USAGE_LEFT: case HID_USAGE_RIGHT:
        return true;
    default: return false;
    }
}

static void draw_key_icon(const kb_key_t *k, int x, int y,
                          int cell_w, int cell_h, uint16_t fg)
{
    int pad = 3;
    int ix = x + pad, iy = y + pad;
    int iw = cell_w - 2 * pad, ih = cell_h - 2 * pad;
    if (iw < 4 || ih < 4) return;
    if (k->special == KB_KEY_SPECIAL_SPACE) {
        icon_space(ix, iy, iw, ih, fg);
        return;
    }
    if (k->special == KB_KEY_SPECIAL_ENTER) {
        icon_enter(ix, iy, iw, ih, fg);
        return;
    }
    if (k->special == KB_KEY_SPECIAL_BACKSPACE) {
        icon_backspace(ix, iy, iw, ih, fg);
        return;
    }
    char dir = 0;
    switch (k->hid_usage) {
    case HID_USAGE_UP:    dir = 'U'; break;
    case HID_USAGE_DOWN:  dir = 'D'; break;
    case HID_USAGE_LEFT:  dir = 'L'; break;
    case HID_USAGE_RIGHT: dir = 'R'; break;
    default: return;
    }
    /* Triangles look balanced when slightly smaller than the
     * padded cell, especially in the constrained-axis direction. */
    int tw = iw * 2 / 3;
    int th = ih * 2 / 3;
    if (tw < 4) tw = iw;
    if (th < 4) th = ih;
    icon_fill_triangle(ix + (iw - tw) / 2, iy + (ih - th) / 2,
                       tw, th, fg, dir);
}

static bool grid_metrics(const kb_layout_t *l,
                         int *out_cell, int *out_x_off, int *out_y_off)
{
    int w = display_width();
    int h = display_height() - STATUS_BAR_H;
    int y0 = STATUS_BAR_H;

    /* Square cells: pick the largest size that fits in both axes.
     * Whatever space is left over (typically vertical -- the panel
     * is wider than tall) becomes equal margins on either side. */
    int cell_w = w / l->cols;
    int cell_h = h / l->rows;
    int cell = cell_w < cell_h ? cell_w : cell_h;
    if (cell < 8) return false;

    if (out_cell)  *out_cell  = cell;
    if (out_x_off) *out_x_off = (w - cell * l->cols) / 2;
    if (out_y_off) *out_y_off = y0 + (h - cell * l->rows) / 2;
    return true;
}

/* Upper-case a Cyrillic codepoint for Shift rendering. Covers the
 * Ukrainian alphabet: the main a..ya block plus Ye / I / Yi which
 * sit outside the regular 0x20 case offset. */
static uint32_t cyr_upper(uint32_t cp)
{
    if (cp >= 0x0430 && cp <= 0x044F) return cp - 0x20;
    switch (cp) {
    case 0x0454: return 0x0404;  /* ye */
    case 0x0456: return 0x0406;  /* i  */
    case 0x0457: return 0x0407;  /* yi */
    default:     return cp;
    }
}

static void draw_keyboard(const theme_t *th)
{
    const kb_layout_t *l = kb_layout_active();
    int cell, x_off, y_off;
    if (!grid_metrics(l, &cell, &x_off, &y_off)) return;

    /* Largest 8x8-multiple that fits inside the cell -- upper bound
     * on text scale for multi-character labels. */
    int max_scale = (cell - 4) / 8;
    if (max_scale < 1) max_scale = 1;
    if (max_scale > 3) max_scale = 3;

    for (int r = 0; r < l->rows; ++r) {
        for (int c = 0; c < l->cols; ++c) {
            const kb_key_t *k = kb_layout_key_at(l, r, c);
            if (!k) continue;
            int x = x_off + c * cell;
            int y = y_off + r * cell;
            bool selected = (r == s_st.sel_row && c == s_st.sel_col);

            uint16_t bg, fg;
            if (selected) {
                bg = th->nav_sel_bg;
                fg = th->nav_sel_fg;
            } else if (k->special != KB_KEY_SPECIAL_NONE) {
                bg = th->key_mod_bg;
                fg = th->key_mod_label;
            } else {
                bg = th->key_bg;
                fg = th->key_label;
            }

            display_fill_rect(x, y, cell, cell, bg);

            if (key_uses_icon(k)) {
                draw_key_icon(k, x, y, cell, cell, fg);
                continue;
            }

            bool shift_on = ((s_st.mod_sticky | s_st.mod_oneshot)
                             & HID_MOD_LSHIFT) != 0;

            /* Non-ASCII single-glyph keys (e.g. the Ukrainian
             * alphabet) carry a Unicode codepoint. Always render the
             * real glyph (upper-cased while Shift is held) at native
             * size, picking the font that best fits the key cell:
             *   - large cells / hi-res panels: the 10x20 Cyrillic font
             *   - low-res 320x240 panels (~18px cells): the smaller
             *     12x16 Cyrillic font, so the glyph stays crisp
             *     instead of being downscaled from 10x20
             *   - tiny cells: the 12x16 font scaled down (a milder,
             *     less lossy shrink than from 10x20)
             * so small panels show real Cyrillic letters, not the
             * ASCII transliteration fallback. */
            if (k->glyph) {
                uint32_t cp = shift_on ? cyr_upper(k->glyph) : k->glyph;
                if (cell >= FONT10X20_W + 2 && cell >= FONT10X20_H + 2) {
                    int tx = x + (cell - FONT10X20_W) / 2;
                    int ty = y + (cell - FONT10X20_H) / 2;
                    display_draw_glyph_10x20_cp(tx, ty, cp, fg, bg, true);
                } else if (cell >= FONT12X16_W + 2 && cell >= FONT12X16_H + 2) {
                    int tx = x + (cell - FONT12X16_W) / 2;
                    int ty = y + (cell - FONT12X16_H) / 2;
                    display_draw_glyph_12x16_cp(tx, ty, cp, fg, bg, true);
                } else {
                    int gh = cell - 4;
                    int gw = gh * FONT12X16_W / FONT12X16_H;
                    if (gw > cell - 2) {
                        gw = cell - 2;
                        gh = gw * FONT12X16_H / FONT12X16_W;
                    }
                    if (gw < 1) gw = 1;
                    if (gh < 1) gh = 1;
                    int tx = x + (cell - gw) / 2;
                    int ty = y + (cell - gh) / 2;
                    display_draw_glyph_12x16_cp_wh(tx, ty, cp, gw, gh,
                                                   fg, bg, true);
                }
                continue;
            }

            const char *lbl = (shift_on && k->label_shifted[0])
                              ? k->label_shifted : k->label_unshifted;
            if (!lbl || !*lbl) continue;

            int lbl_len = (int)strlen(lbl);

            /* Single-character labels (the letters, digits and
             * punctuation that dominate the grid) render through the
             * same native-font selection as the Cyrillic glyphs above
             * so Latin and Cyrillic look identical:
             *   - large cells / hi-res panels: the 10x20 font
             *   - low-res 320x240 panels (~18px cells): the smaller
             *     12x16 font, kept crisp instead of downscaled
             *   - tiny cells: the 12x16 font scaled down
             * Multi-character labels (Esc, Caps, Bksp, F1..F12) stay
             * on the 8x8 font with the fit-to-cell logic below, which
             * keeps those 2-3 letter labels readable. */
            if (lbl_len == 1) {
                uint32_t cp = (uint32_t)(unsigned char)lbl[0];
                if (cell >= FONT10X20_W + 2 && cell >= FONT10X20_H + 2) {
                    int tx = x + (cell - FONT10X20_W) / 2;
                    int ty = y + (cell - FONT10X20_H) / 2;
                    display_draw_glyph_10x20_cp(tx, ty, cp, fg, bg, true);
                } else if (cell >= FONT12X16_W + 2 && cell >= FONT12X16_H + 2) {
                    int tx = x + (cell - FONT12X16_W) / 2;
                    int ty = y + (cell - FONT12X16_H) / 2;
                    display_draw_glyph_12x16_cp(tx, ty, cp, fg, bg, true);
                } else {
                    int gh = cell - 4;
                    int gw = gh * FONT12X16_W / FONT12X16_H;
                    if (gw > cell - 2) {
                        gw = cell - 2;
                        gh = gw * FONT12X16_H / FONT12X16_W;
                    }
                    if (gw < 1) gw = 1;
                    if (gh < 1) gh = 1;
                    int tx = x + (cell - gw) / 2;
                    int ty = y + (cell - gh) / 2;
                    display_draw_glyph_12x16_cp_wh(tx, ty, cp, gw, gh,
                                                   fg, bg, true);
                }
                continue;
            }

            /* Shrink multi-letter labels so they fit the cell:
             * pick the largest 8x8 scale (capped by max_scale) that
             * leaves a small horizontal margin. Single-glyph keys
             * therefore stay big, while "Caps" / "Bksp" / "PgUp"
             * drop down to a smaller, fully readable size.
             *
             * Function keys (F1..F12) all use the same reference
             * width of 3 chars so that F1..F9 render at the same
             * scale as F10..F12 -- the row looks uniform instead
             * of having two visually distinct tiers. */
            int fit_len = lbl_len;
            if (k->special == KB_KEY_SPECIAL_FN && fit_len < 3) {
                fit_len = 3;
            }
            int fit_scale = (cell - 2) / (fit_len * 8);
            if (fit_scale < 1) fit_scale = 1;
            int scale = fit_scale < max_scale ? fit_scale : max_scale;
            int gw = 8 * scale;

            /* On small cells the integer-scaled 8x8 font cannot go
             * below 8 px, so a two- or three-letter label would fill
             * (or overflow) the whole cell. When even scale 1 leaves
             * no comfortable margin, fall back to a horizontally
             * condensed glyph (sub-8-px wide, drawn via the
             * nearest-neighbour scaler) so the label shrinks to a
             * clearly smaller font that fits with a margin. */
            int want_w = cell - 4;   /* target width, ~2 px margin  */
            int max_w  = cell - 2;   /* hard upper bound            */
            if (fit_len * gw <= want_w) {
                int tx = x + (cell - lbl_len * gw) / 2;
                int ty = y + (cell - gw) / 2;
                display_draw_string(tx, ty, lbl, scale, fg, bg, true);
            } else {
                int cw = want_w / fit_len;
                if (cw < 4) cw = 4;                 /* legibility floor */
                if (cw * fit_len > max_w) cw = max_w / fit_len;
                if (cw < 1) cw = 1;
                int ch = 8;
                if (ch > cell - 2) ch = cell - 2;
                int tx = x + (cell - lbl_len * cw) / 2;
                int ty = y + (cell - ch) / 2;
                display_draw_string_wh(tx, ty, lbl, cw, ch, fg, bg, true);
            }
        }
    }
}

static void draw_mouse_overlay(const theme_t *th)
{
    int w = display_width();
    int y0 = STATUS_BAR_H;
    int h = display_height() - STATUS_BAR_H;
    display_fill_rect(0, y0, w, h, th->win_bg);
    const char *msg = "MOUSE MODE";
    int len = (int)strlen(msg);
    int scale = 3;
    display_draw_string((w - len * 8 * scale) / 2,
                        y0 + (h - 8 * scale) / 2,
                        msg, scale, th->key_label, th->win_bg, true);
}

/* ----- Settings menu ----- */
/*
 * A small modal list navigated by the gamepad: up/down move the
 * cursor, left/right change the highlighted item's value, and the
 * action button activates it. Items:
 *   0            Theme           (left/right cycles the palette)
 *   1            Mouse speed     (left/right changes the speed)
 *   2            Sound volume    (left/right changes the volume)
 *   3            Nav rollover    (left/right/action toggles on/off)
 *   4..n_lang+3  Lang <NAME>     (left/right/action toggles enable)
 *   n_lang+4     Close
 */

/* Fixed (non-language) rows that precede the language list. */
#define MENU_ROW_THEME        0
#define MENU_ROW_MOUSE_SPEED  1
#define MENU_ROW_SOUND_VOL    2
#define MENU_ROW_NAV_ROLLOVER 3
#define MENU_FIXED_ROWS       4

static int menu_lang_count(void)
{
    int n = 0;
    for (int i = 0; i < kb_layout_count(); ++i) {
        if (kb_layout_is_available(i)) ++n;
    }
    return n;
}
/* Rows: fixed rows + language rows + a trailing Close row. */
static int menu_item_count(void)
{
    return MENU_FIXED_ROWS + menu_lang_count() + 1;
}
static int menu_close_index(void)
{
    return MENU_FIXED_ROWS + menu_lang_count();
}

/* Map the nth (0-based) language row to its global layout index,
 * skipping layouts not activated in Kconfig. Returns -1 if out of
 * range. */
static int menu_lang_layout_index(int nth)
{
    int c = 0;
    for (int i = 0; i < kb_layout_count(); ++i) {
        if (!kb_layout_is_available(i)) continue;
        if (c == nth) return i;
        ++c;
    }
    return -1;
}

static void menu_item_text(int idx, char *out, size_t n)
{
    int nl = menu_lang_count();
    if (idx == MENU_ROW_THEME) {
        snprintf(out, n, "Theme: %s", theme_active()->name);
    } else if (idx == MENU_ROW_MOUSE_SPEED) {
        snprintf(out, n, "Mouse speed: %d/%d",
                 s_st.mouse_speed + 1, KB_MOUSE_SPEED_LEVELS);
    } else if (idx == MENU_ROW_SOUND_VOL) {
        int vol = 0;
#if CONFIG_BOARD_HAS_SPEAKER
        vol = audio_get_volume();
#endif
        snprintf(out, n, "Sound volume: %d%%", vol);
    } else if (idx == MENU_ROW_NAV_ROLLOVER) {
        snprintf(out, n, "Nav rollover: %s", s_st.nav_rollover ? "ON" : "off");
    } else if (idx >= MENU_FIXED_ROWS && idx < MENU_FIXED_ROWS + nl) {
        int li = menu_lang_layout_index(idx - MENU_FIXED_ROWS);
        const kb_layout_t *l = kb_layout_by_index(li);
        snprintf(out, n, "Lang %s: %s",
                 l ? l->name : "?",
                 kb_layout_is_enabled(li) ? "ON" : "off");
    } else {
        snprintf(out, n, "Close");
    }
}

static void draw_menu(const theme_t *th)
{
    int w = display_width();
    int y0 = STATUS_BAR_H;
    int h = display_height() - STATUS_BAR_H;
    display_fill_rect(0, y0, w, h, th->win_bg);

    int n = menu_item_count();

    /* Measure the widest row (and the title) so the text scale can
     * be chosen to keep every line inside the list box. The menu
     * must fit on small 320x240 panels as well as the larger
     * 480x320 ones, so nothing here is a fixed pixel size. */
    const char *title = "SETTINGS";
    char line[40];
    int max_len = (int)strlen(title);
    for (int i = 0; i < n; ++i) {
        menu_item_text(i, line, sizeof(line));
        int len = (int)strlen(line);
        if (len > max_len) max_len = len;
    }
    if (max_len < 1) max_len = 1;

    int box_x = w / 8;
    int box_w = w - w / 4;      /* highlighted row width          */
    int text_pad = 8;           /* left padding of the row text   */
    int avail_w = box_w - 2 * text_pad;

    /* Largest integer 8x8 scale whose widest row still fits the box
     * width; capped at 2 (the previous fixed size). */
    int text_scale = avail_w / (max_len * 8);
    if (text_scale < 1) text_scale = 1;
    if (text_scale > 2) text_scale = 2;

    /* Vertical budget: title band then the item list. */
    int title_scale = text_scale;
    int title_h = 8 * title_scale;
    int top = y0 + 8 + title_h + 8;
    int avail_h = y0 + h - top - 4;

    /* Row height fits all rows in the remaining space, capped so the
     * list doesn't look sparse on tall panels. */
    int row_h = (n > 0) ? avail_h / n : avail_h;
    if (row_h > 28) row_h = 28;
    if (row_h < 8)  row_h = 8;

    /* Shrink the text if a row is too short for the chosen scale. */
    while (text_scale > 1 && 8 * text_scale > row_h - 2) {
        text_scale--;
    }

    display_draw_string((w - (int)strlen(title) * 8 * title_scale) / 2,
                        y0 + 8, title, title_scale,
                        th->key_label, th->win_bg, true);

    /* Vertically centre the list in the remaining space. */
    int list_h = n * row_h;
    if (avail_h > list_h) top += (avail_h - list_h) / 2;

    for (int i = 0; i < n; ++i) {
        bool sel = (i == s_st.menu_sel);
        int y = top + i * row_h;
        uint16_t bg = sel ? th->nav_sel_bg : th->win_bg;
        uint16_t fg = sel ? th->nav_sel_fg : th->key_label;
        display_fill_rect(box_x, y, box_w, row_h - 2, bg);
        menu_item_text(i, line, sizeof(line));
        int ty = y + (row_h - 2 - 8 * text_scale) / 2;
        display_draw_string(box_x + text_pad, ty, line, text_scale,
                            fg, bg, true);
    }
}

static void redraw_all(void)
{
    const theme_t *th = theme_active();
    display_clear(th->win_bg);
    draw_status_bar(th);
    if (s_st.mode == KB_MODE_MENU) {
        draw_menu(th);
    } else if (s_st.mode == KB_MODE_MOUSE) {
        draw_mouse_overlay(th);
    } else {
        draw_keyboard(th);
    }
    display_flush();
}

/* ----- Task / queue ----- */

static void ui_task(void *arg)
{
    (void)arg;
    uint8_t dummy;
    /* Initial paint */
    redraw_all();
    while (1) {
        if (xQueueReceive(s_redraw_q, &dummy, portMAX_DELAY) == pdTRUE) {
            /* Coalesce: drain anything queued behind us. */
            while (xQueueReceive(s_redraw_q, &dummy, 0) == pdTRUE) { }
            redraw_all();
        }
    }
}

/* ----- Public API ----- */

void keyboard_ui_init(void)
{
    memset(&s_st, 0, sizeof(s_st));
    s_st.sel_row = 0;
    s_st.sel_col = 0;
    s_st.mode    = KB_MODE_KEYBOARD;
    s_st.mouse_speed = MOUSE_SPEED_DEFAULT;
    snprintf(s_st.hid_status, sizeof(s_st.hid_status), "HID: idle");

    nvs_load_strings();
    s_redraw_q = xQueueCreate(4, sizeof(uint8_t));
    ESP_LOGI(TAG, "ready; layout=%s theme=%s",
             kb_layout_active()->name, theme_active()->name);
}

void keyboard_ui_start_task(void)
{
    xTaskCreatePinnedToCore(ui_task, "kb_ui", 6144, NULL, 4, NULL, 0);
}

void keyboard_ui_request_redraw(void)
{
    if (!s_redraw_q) return;
    uint8_t tok = 1;
    xQueueSend(s_redraw_q, &tok, 0);  /* never blocks */
}

void keyboard_ui_redraw_now(void)
{
    /* Synchronous full redraw from the calling context. Used by
     * app_main() to paint the keyboard once before HID / gamepad
     * init begins, so the user sees a visible UI even while those
     * (potentially long-running) subsystems come up. */
    redraw_all();
}

bool keyboard_ui_move(int drow, int dcol)
{
    const kb_layout_t *l = kb_layout_active();
    int nr = s_st.sel_row + drow;
    int nc = s_st.sel_col + dcol;
    if (s_st.nav_rollover) {
        nr = ((nr % l->rows) + l->rows) % l->rows;
        nc = ((nc % l->cols) + l->cols) % l->cols;
    } else {
        if (nr < 0) nr = 0;
        if (nr >= l->rows) nr = l->rows - 1;
        if (nc < 0) nc = 0;
        if (nc >= l->cols) nc = l->cols - 1;
    }
    if (nr == s_st.sel_row && nc == s_st.sel_col) return false;
    s_st.sel_row = nr;
    s_st.sel_col = nc;
    keyboard_ui_request_redraw();
    return true;
}

void keyboard_ui_toggle_mod(uint8_t mod)
{
    if (mod == 0) {
        s_st.mod_sticky = 0;
        s_st.mod_oneshot = 0;
    } else {
        s_st.mod_sticky ^= mod;
    }
    keyboard_ui_request_redraw();
}

void keyboard_ui_oneshot_mod(uint8_t mod)
{
    s_st.mod_oneshot |= mod;
    keyboard_ui_request_redraw();
}

/* Map a modifier key cell to its HID modifier bit (0 if the cell
 * is not a Shift / Ctrl / Alt / AltGr / Win key). Modifier cells
 * carry HID_USAGE_NONE in the layout and are distinguished by
 * their idle label, mirroring the narrator's approach. */
static uint8_t key_mod_bit(const kb_key_t *k)
{
    const char *l = k ? k->label_unshifted : NULL;
    if (!l || !*l) return 0;
    if (strcmp(l, "Sft") == 0) return HID_MOD_LSHIFT;
    if (strcmp(l, "Ctl") == 0) return HID_MOD_LCTRL;
    if (strcmp(l, "Alt") == 0) return HID_MOD_LALT;
    if (strcmp(l, "AGr") == 0) return HID_MOD_RALT;
    if (strcmp(l, "Win") == 0) return HID_MOD_LGUI;
    return 0;
}

void keyboard_ui_press_current(void)
{
    const kb_layout_t *l = kb_layout_active();
    const kb_key_t *k = kb_layout_key_at(l, s_st.sel_row, s_st.sel_col);
    if (!k) return;

    /* UI action keys are handled locally, not sent over HID. */
    if (k->special == KB_KEY_SPECIAL_LANG) {
        keyboard_ui_cycle_layout();
        return;
    }
    if (k->special == KB_KEY_SPECIAL_MENU) {
        keyboard_ui_open_menu();
        return;
    }

    /* Modifier cells toggle a sticky modifier latch. The latch
     * persists across navigation and is cleared once a real letter
     * or symbol is pressed (below), so modifiers behave "sticky
     * until the next character". */
    uint8_t mb = key_mod_bit(k);
    if (mb) {
        if (mb == HID_MOD_LGUI) {
            uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
            if (now - s_st.win_last_press_ms <= WIN_DOUBLE_TAP_MS) {
                hid_send_key(HID_MOD_LGUI, HID_USAGE_NONE);
                hid_release_all();
                s_st.win_last_press_ms = 0;
            } else {
                s_st.win_last_press_ms = now;
            }
        }
        s_st.mod_sticky ^= mb;
        keyboard_ui_request_redraw();
        return;
    }

    if (k->hid_usage == HID_USAGE_NONE) return;

    uint8_t mods = s_st.mod_sticky | s_st.mod_oneshot;
    bool shifted = (mods & (HID_MOD_LSHIFT | HID_MOD_RSHIFT)) != 0;
    hid_send_key(mods, k->hid_usage);
    hid_release_all();
    /* Speak the letter / symbol we just sent (shift-aware). */
    narrator_speak_key_ex(k, shifted);
    /* A character press consumes the one-shot and sticky modifier
     * latches. */
    s_st.mod_oneshot = 0;
    s_st.mod_sticky = 0;
    keyboard_ui_request_redraw();
}

void keyboard_ui_set_hid_status(const char *text, bool connected)
{
    s_st.hid_connected = connected;
    if (text) {
        snprintf(s_st.hid_status, sizeof(s_st.hid_status), "%s", text);
    }
    /* Clear any stale pairing PIN once we transition to connected. */
    if (connected) s_st.passkey = 0;
    keyboard_ui_request_redraw();
}

void keyboard_ui_set_passkey(uint32_t passkey)
{
    s_st.passkey = passkey;
    keyboard_ui_request_redraw();
}

void keyboard_ui_set_mode(keyboard_ui_mode_t mode)
{
    if (s_st.mode == mode) return;
    s_st.mode = mode;
    keyboard_ui_request_redraw();
}

keyboard_ui_mode_t keyboard_ui_get_mode(void) { return s_st.mode; }

int keyboard_ui_selected_hid_usage(void)
{
    const kb_layout_t *l = kb_layout_active();
    const kb_key_t *k = kb_layout_key_at(l, s_st.sel_row, s_st.sel_col);
    return k ? k->hid_usage : HID_USAGE_NONE;
}

const kb_key_t *keyboard_ui_selected_key(void)
{
    const kb_layout_t *l = kb_layout_active();
    return kb_layout_key_at(l, s_st.sel_row, s_st.sel_col);
}

/* Host-side layout switch. When the active layout changes we emit
 * Ctrl+Shift+<digit> so a host configured with matching language
 * hotkeys follows the device. The per-language digit comes from
 * Kconfig (SK_LAYOUT_SWITCH_DIGIT_*); 0 disables the report. */
static int layout_switch_digit(const kb_layout_t *l)
{
    if (!l) return 0;
    if (strcmp(l->name, "US") == 0) return CONFIG_SK_LAYOUT_SWITCH_DIGIT_US;
    if (strcmp(l->name, "UA") == 0) return CONFIG_SK_LAYOUT_SWITCH_DIGIT_UA;
    return 0;
}

static void send_layout_switch_hid(const kb_layout_t *l)
{
    int d = layout_switch_digit(l);
    if (d < 1 || d > 9) return;  /* 0 (or out of range) = disabled */
    uint8_t usage = (uint8_t)(HID_USAGE_1 + (d - 1));
    hid_send_key(HID_MOD_LCTRL | HID_MOD_LSHIFT, usage);
    hid_release_all();
}

void keyboard_ui_cycle_layout(void)
{
    const kb_layout_t *cur = kb_layout_active();
    const kb_layout_t *nxt = kb_layout_next_enabled(cur);
    if (!nxt) nxt = cur;
    kb_layout_set_active_by_name(nxt->name);
    if (nxt != cur) send_layout_switch_hid(nxt);
    /* Clamp selection to new grid bounds. */
    if (s_st.sel_row >= nxt->rows) s_st.sel_row = nxt->rows - 1;
    if (s_st.sel_col >= nxt->cols) s_st.sel_col = nxt->cols - 1;
    nvs_save_strings();
    keyboard_ui_request_redraw();
}

void keyboard_ui_cycle_theme(void)
{
    const theme_t *cur = theme_active();
    int n = theme_count();
    int idx = 0;
    for (int i = 0; i < n; ++i) {
        if (theme_by_index(i) == cur) { idx = i; break; }
    }
    const theme_t *nxt = theme_by_index((idx + 1) % n);
    theme_set_active_by_name(nxt->name);
    nvs_save_strings();
    keyboard_ui_request_redraw();
}

int keyboard_ui_mouse_max_step(void)
{
    int i = s_st.mouse_speed;
    if (i < 0 || i >= KB_MOUSE_SPEED_LEVELS) i = MOUSE_SPEED_DEFAULT;
    return s_mouse_speed_step[i];
}

/* Step the mouse-speed level by delta (wrapping), persist it, and
 * redraw so the menu reflects the new value. */
static void mouse_speed_adjust(int delta)
{
    int n = KB_MOUSE_SPEED_LEVELS;
    int v = s_st.mouse_speed + delta;
    while (v < 0)  v += n;
    while (v >= n) v -= n;
    s_st.mouse_speed = v;
    nvs_save_strings();
    keyboard_ui_request_redraw();
}

static void sound_volume_adjust(int delta)
{
#if CONFIG_BOARD_HAS_SPEAKER
    int vol = audio_get_volume();
    if( delta > 0 ) {
        if( vol <= 100 ) vol += 10;
    }
    else {
        if( vol >= 10 ) vol -= 10;
    }
    audio_set_volume(vol);
    nvs_save_strings();
    keyboard_ui_request_redraw();
    audio_play_startup_tune();
#endif
}

int keyboard_ui_selected_shift(void)
{
    return (s_st.mod_sticky | s_st.mod_oneshot)
           & (HID_MOD_LSHIFT | HID_MOD_RSHIFT);
}

void keyboard_ui_narrate_selection(void)
{
    const kb_key_t *k = keyboard_ui_selected_key();
    if (!k) return;
    narrator_speak_key_ex(k, keyboard_ui_selected_shift() != 0);
}

/* ----- Settings menu ----- */

void keyboard_ui_open_menu(void)
{
    s_st.menu_sel = 0;
    keyboard_ui_set_mode(KB_MODE_MENU);
}

void keyboard_ui_close_menu(void)
{
    keyboard_ui_set_mode(KB_MODE_KEYBOARD);
}

void keyboard_ui_menu_move(int delta)
{
    if (s_st.mode != KB_MODE_MENU) return;
    int n = menu_item_count();
    int sel = s_st.menu_sel + delta;
    if (sel < 0) sel = n - 1;
    if (sel >= n) sel = 0;
    s_st.menu_sel = sel;
    keyboard_ui_request_redraw();
}

/* Apply a value change to the highlighted item. For Theme this
 * cycles the palette; for a language row it toggles its enabled
 * flag; the Close row has no value. */
void keyboard_ui_menu_adjust(int delta)
{
    if (s_st.mode != KB_MODE_MENU) return;
    int nl = menu_lang_count();
    int idx = s_st.menu_sel;
    if (idx == MENU_ROW_THEME) {
        (void)delta;
        keyboard_ui_cycle_theme();   /* persists + redraws */
    } else if (idx == MENU_ROW_MOUSE_SPEED) {
        mouse_speed_adjust(delta ? delta : 1);  /* persists + redraws */
    } else if (idx == MENU_ROW_SOUND_VOL) {
        sound_volume_adjust(delta);
    } else if (idx == MENU_ROW_NAV_ROLLOVER) {
        (void)delta;
        s_st.nav_rollover = !s_st.nav_rollover;
        nvs_save_strings();
        keyboard_ui_request_redraw();
    } else if (idx >= MENU_FIXED_ROWS && idx < MENU_FIXED_ROWS + nl) {
        int li = menu_lang_layout_index(idx - MENU_FIXED_ROWS);
        if (li < 0) return;
        const kb_layout_t *before = kb_layout_active();
        kb_layout_set_enabled(li, !kb_layout_is_enabled(li));
        /* Disabling the active layout can switch the active layout
         * (kb_layout keeps the active one in the enabled set); make
         * sure the selection cursor stays within the new grid. */
        const kb_layout_t *a = kb_layout_active();
        if (a != before) send_layout_switch_hid(a);
        if (s_st.sel_row >= a->rows) s_st.sel_row = a->rows - 1;
        if (s_st.sel_col >= a->cols) s_st.sel_col = a->cols - 1;
        nvs_save_strings();
        keyboard_ui_request_redraw();
    }
}

void keyboard_ui_menu_select(void)
{
    if (s_st.mode != KB_MODE_MENU) return;
    if (s_st.menu_sel == menu_close_index()) {
        keyboard_ui_close_menu();
        return;
    }
    keyboard_ui_menu_adjust(1);
}
