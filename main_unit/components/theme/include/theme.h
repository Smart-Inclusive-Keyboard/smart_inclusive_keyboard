#pragma once

/*
 * Color themes for the Smart Keyboard UI.
 *
 * The palette names mirror the upstream Rust project's
 * theme_*.toml files so that the same visual identity carries
 * over. Colors are packed RGB565 (the native pixel format of the
 * display framebuffer); helpers below convert from #RRGGBB.
 *
 * A compile-time default is chosen via CONFIG_SK_THEME_*; the
 * runtime selection (overridden via the on-screen menu) lives in
 * theme_set_active() and is persisted to NVS by keyboard_ui.
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* Background of the whole window / display. */
    uint16_t win_bg;

    /* Regular key cell background and label. */
    uint16_t key_bg;
    uint16_t key_label;

    /* Modifier (Shift/Ctrl/Alt) key cell, idle and engaged. */
    uint16_t key_mod_bg;
    uint16_t key_mod_label;
    uint16_t mod_active_bg;
    uint16_t mod_active_fg;

    /* Currently selected (cursor) cell. */
    uint16_t nav_sel_bg;
    uint16_t nav_sel_fg;

    /* Status bar at top/bottom of the screen. */
    uint16_t status_bar_bg;
    uint16_t status_ind_fg;
    uint16_t status_ind_active_bg;
    uint16_t status_ind_active_fg;

    /* BLE connection indicator. */
    uint16_t conn_disconnected;
    uint16_t conn_connecting;
    uint16_t conn_connected;

    const char *name;  /* short identifier persisted to NVS */
} theme_t;

/* Convert 0xRRGGBB (24-bit) to RGB565. */
static inline uint16_t theme_rgb565(uint32_t rgb888)
{
    uint8_t r = (rgb888 >> 16) & 0xFF;
    uint8_t g = (rgb888 >>  8) & 0xFF;
    uint8_t b = (rgb888      ) & 0xFF;
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

/* The currently active theme (defaults to the one chosen via
 * Kconfig at boot, then to whatever theme_set_active_by_name()
 * was last given, persisted in NVS). Never NULL. */
const theme_t *theme_active(void);

/* Iterate over all built-in themes. Returns NULL after the last
 * entry. */
int                theme_count(void);
const theme_t *    theme_by_index(int i);
const theme_t *    theme_by_name(const char *name);

/* Switch the active theme. Returns true if `name` matched one of
 * the built-ins, false otherwise (active theme unchanged). The
 * keyboard_ui layer is responsible for persisting the choice to
 * NVS and triggering a redraw. */
bool theme_set_active_by_name(const char *name);

#ifdef __cplusplus
}
#endif
