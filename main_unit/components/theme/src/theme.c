/*
 * Built-in theme table. Palettes are ported one-to-one from the
 * upstream Rust project's theme_*.toml files so the visual
 * identity is preserved.
 */

#include "theme.h"

#include <string.h>

#include "sdkconfig.h"

/* Helper: pack an #RRGGBB literal at compile time as RGB565. */
#define RGB(x)  ((uint16_t)(                          \
        (((((x) >> 16) & 0xFF) & 0xF8) << 8)        | \
        (((((x) >>  8) & 0xFF) & 0xFC) << 3)        | \
        ((((x))       & 0xFF) >> 3)))

static const theme_t s_themes[] = {
    /* Green on black -- the documented default. */
    {
        .name                  = "green_on_black",
        .win_bg                = RGB(0x000000),
        .key_bg                = RGB(0x000000),
        .key_label             = RGB(0x00FF00),
        .key_mod_bg            = RGB(0x000000),
        .key_mod_label         = RGB(0x00FF00),
        .mod_active_bg         = RGB(0x00FF00),
        .mod_active_fg         = RGB(0x000000),
        .nav_sel_bg            = RGB(0x00FF00),
        .nav_sel_fg            = RGB(0x000000),
        .status_bar_bg         = RGB(0x000000),
        .status_ind_fg         = RGB(0x00FF00),
        .status_ind_active_bg  = RGB(0x003300),
        .status_ind_active_fg  = RGB(0x00FF00),
        .conn_disconnected     = RGB(0x004400),
        .conn_connecting       = RGB(0x00AA00),
        .conn_connected        = RGB(0x00FF00),
    },
    {
        .name                  = "darkgreen_on_black",
        .win_bg                = RGB(0x000000),
        .key_bg                = RGB(0x000000),
        .key_label             = RGB(0x008800),
        .key_mod_bg            = RGB(0x000000),
        .key_mod_label         = RGB(0x008800),
        .mod_active_bg         = RGB(0x008800),
        .mod_active_fg         = RGB(0x000000),
        .nav_sel_bg            = RGB(0x008800),
        .nav_sel_fg            = RGB(0x000000),
        .status_bar_bg         = RGB(0x000000),
        .status_ind_fg         = RGB(0x008800),
        .status_ind_active_bg  = RGB(0x002200),
        .status_ind_active_fg  = RGB(0x008800),
        .conn_disconnected     = RGB(0x002200),
        .conn_connecting       = RGB(0x005500),
        .conn_connected        = RGB(0x008800),
    },
    {
        .name                  = "white_on_black",
        .win_bg                = RGB(0x000000),
        .key_bg                = RGB(0x000000),
        .key_label             = RGB(0xFFFFFF),
        .key_mod_bg            = RGB(0x000000),
        .key_mod_label         = RGB(0xFFFFFF),
        .mod_active_bg         = RGB(0xFFFFFF),
        .mod_active_fg         = RGB(0x000000),
        .nav_sel_bg            = RGB(0xFFFFFF),
        .nav_sel_fg            = RGB(0x000000),
        .status_bar_bg         = RGB(0x000000),
        .status_ind_fg         = RGB(0xFFFFFF),
        .status_ind_active_bg  = RGB(0x444444),
        .status_ind_active_fg  = RGB(0xFFFFFF),
        .conn_disconnected     = RGB(0x444444),
        .conn_connecting       = RGB(0xAAAAAA),
        .conn_connected        = RGB(0xFFFFFF),
    },
    {
        .name                  = "black_on_white",
        .win_bg                = RGB(0xFFFFFF),
        .key_bg                = RGB(0xFFFFFF),
        .key_label             = RGB(0x000000),
        .key_mod_bg            = RGB(0xFFFFFF),
        .key_mod_label         = RGB(0x000000),
        .mod_active_bg         = RGB(0x000000),
        .mod_active_fg         = RGB(0xFFFFFF),
        .nav_sel_bg            = RGB(0x000000),
        .nav_sel_fg            = RGB(0xFFFFFF),
        .status_bar_bg         = RGB(0xFFFFFF),
        .status_ind_fg         = RGB(0x000000),
        .status_ind_active_bg  = RGB(0xBBBBBB),
        .status_ind_active_fg  = RGB(0x000000),
        .conn_disconnected     = RGB(0xBBBBBB),
        .conn_connecting       = RGB(0x666666),
        .conn_connected        = RGB(0x000000),
    },
    {
        .name                  = "default",
        .win_bg                = RGB(0x202020),
        .key_bg                = RGB(0x303030),
        .key_label             = RGB(0xE0E0E0),
        .key_mod_bg            = RGB(0x404040),
        .key_mod_label         = RGB(0xE0E0E0),
        .mod_active_bg         = RGB(0x6090C0),
        .mod_active_fg         = RGB(0x000000),
        .nav_sel_bg            = RGB(0xE0A020),
        .nav_sel_fg            = RGB(0x000000),
        .status_bar_bg         = RGB(0x101010),
        .status_ind_fg         = RGB(0xC0C0C0),
        .status_ind_active_bg  = RGB(0x303030),
        .status_ind_active_fg  = RGB(0xE0E0E0),
        .conn_disconnected     = RGB(0x600000),
        .conn_connecting       = RGB(0xC08000),
        .conn_connected        = RGB(0x00A000),
    },
};

static const int s_theme_count = sizeof(s_themes) / sizeof(s_themes[0]);

static const theme_t *s_active = &s_themes[0];

static const char *kconfig_default_name(void)
{
#if defined(CONFIG_SK_THEME_GREEN_ON_BLACK)
    return "green_on_black";
#elif defined(CONFIG_SK_THEME_DARKGREEN_ON_BLACK)
    return "darkgreen_on_black";
#elif defined(CONFIG_SK_THEME_WHITE_ON_BLACK)
    return "white_on_black";
#elif defined(CONFIG_SK_THEME_BLACK_ON_WHITE)
    return "black_on_white";
#elif defined(CONFIG_SK_THEME_DEFAULT_PALETTE)
    return "default";
#else
    return "green_on_black";
#endif
}

static void resolve_kconfig_default_once(void)
{
    static bool s_done;
    if (s_done) return;
    s_done = true;
    const theme_t *t = theme_by_name(kconfig_default_name());
    if (t) s_active = t;
}

const theme_t *theme_active(void)
{
    resolve_kconfig_default_once();
    return s_active;
}

int theme_count(void) { return s_theme_count; }

const theme_t *theme_by_index(int i)
{
    if (i < 0 || i >= s_theme_count) return NULL;
    return &s_themes[i];
}

const theme_t *theme_by_name(const char *name)
{
    if (!name) return NULL;
    for (int i = 0; i < s_theme_count; ++i) {
        if (strcmp(s_themes[i].name, name) == 0) return &s_themes[i];
    }
    return NULL;
}

bool theme_set_active_by_name(const char *name)
{
    const theme_t *t = theme_by_name(name);
    if (!t) return false;
    s_active = t;
    return true;
}
