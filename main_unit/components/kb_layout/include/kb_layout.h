#pragma once

/*
 * Keyboard layout data.
 *
 * A layout is a static table of `kb_key_t` entries arranged in a
 * rectangular grid (rows x cols). The grid drives both the
 * on-screen rendering (keyboard_ui) and the HID dispatch
 * (input_router): given a row/col selection and a modifier mask,
 * input_router emits modifier + HID-usage to the active HID
 * transport (BLE or USB, see components/hid).
 *
 * Ported one-to-one from the upstream Rust project's keymap_*.toml
 * so behaviour (incl. shifted glyphs) matches.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Display label is an ASCII string (1-3 chars) for portability
 * with the embedded 8x8 font. Non-Latin layouts (e.g. UA) keep an
 * ASCII transliteration here as a fallback but set the `glyph`
 * field below to the real Unicode codepoint, which the renderer
 * draws from the 10x20 font's embedded non-ASCII glyphs. */
typedef struct {
    const char *label_unshifted;   /* what we draw idle    */
    const char *label_shifted;     /* what we draw w/Shift */
    uint8_t     hid_usage;         /* USB HID usage ID     */
    /* If non-zero, this key is a "special" (Backspace, Enter,
     * Space, Tab, Escape) and should be rendered with an
     * accent color. */
    uint8_t     special;
    /* Optional narrator clip tokens. When non-NULL, the narrator
     * plays the named clip (see components/narrator/wav/<token>.wav)
     * instead of deriving the clip from the HID usage. Used by
     * layouts whose on-screen labels are ASCII transliterations of
     * non-Latin glyphs (e.g. the Ukrainian layout), so the spoken
     * name matches the character the host actually receives. NULL
     * falls back to the HID-usage / label based lookup. */
    const char *sound_unshifted;
    const char *sound_shifted;
    /* Optional Unicode codepoint to render on-screen instead of the
     * ASCII label. 0 means "use label_unshifted / label_shifted".
     * Non-Latin layouts (e.g. Ukrainian) set this to the actual
     * alphabet letter so the on-screen key shows the real glyph
     * while label_unshifted stays an ASCII transliteration used as
     * a fallback when the cell is too small for the 10x20 glyph.
     * When a Shift modifier is active the renderer draws the
     * upper-case form of this codepoint. */
    uint16_t    glyph;
} kb_key_t;

typedef struct {
    const char     *name;          /* "US", "DE", "FR", "UA"  */
    int             rows;
    int             cols;
    const kb_key_t *keys;           /* rows * cols entries     */
} kb_layout_t;

/* HID Usage IDs for the most common keys, mirroring USB HID
 * Keyboard/Keypad Page (0x07). Full table:
 * https://usb.org/sites/default/files/hut1_22.pdf */
#define HID_USAGE_NONE       0x00
#define HID_USAGE_A          0x04
#define HID_USAGE_B          0x05
#define HID_USAGE_C          0x06
#define HID_USAGE_D          0x07
#define HID_USAGE_E          0x08
#define HID_USAGE_F          0x09
#define HID_USAGE_G          0x0A
#define HID_USAGE_H          0x0B
#define HID_USAGE_I          0x0C
#define HID_USAGE_J          0x0D
#define HID_USAGE_K          0x0E
#define HID_USAGE_L          0x0F
#define HID_USAGE_M          0x10
#define HID_USAGE_N          0x11
#define HID_USAGE_O          0x12
#define HID_USAGE_P          0x13
#define HID_USAGE_Q          0x14
#define HID_USAGE_R          0x15
#define HID_USAGE_S          0x16
#define HID_USAGE_T          0x17
#define HID_USAGE_U          0x18
#define HID_USAGE_V          0x19
#define HID_USAGE_W          0x1A
#define HID_USAGE_X          0x1B
#define HID_USAGE_Y          0x1C
#define HID_USAGE_Z          0x1D
#define HID_USAGE_1          0x1E
#define HID_USAGE_2          0x1F
#define HID_USAGE_3          0x20
#define HID_USAGE_4          0x21
#define HID_USAGE_5          0x22
#define HID_USAGE_6          0x23
#define HID_USAGE_7          0x24
#define HID_USAGE_8          0x25
#define HID_USAGE_9          0x26
#define HID_USAGE_0          0x27
#define HID_USAGE_ENTER      0x28
#define HID_USAGE_ESCAPE     0x29
#define HID_USAGE_BACKSPACE  0x2A
#define HID_USAGE_TAB        0x2B
#define HID_USAGE_SPACE      0x2C
#define HID_USAGE_MINUS      0x2D
#define HID_USAGE_EQUAL      0x2E
#define HID_USAGE_LBRACKET   0x2F
#define HID_USAGE_RBRACKET   0x30
#define HID_USAGE_BACKSLASH  0x31
#define HID_USAGE_SEMICOLON  0x33
#define HID_USAGE_APOSTROPHE 0x34
#define HID_USAGE_GRAVE      0x35
#define HID_USAGE_COMMA      0x36
#define HID_USAGE_PERIOD     0x37
#define HID_USAGE_SLASH      0x38
#define HID_USAGE_CAPSLOCK   0x39
#define HID_USAGE_F1         0x3A
#define HID_USAGE_F2         0x3B
#define HID_USAGE_F3         0x3C
#define HID_USAGE_F4         0x3D
#define HID_USAGE_F5         0x3E
#define HID_USAGE_F6         0x3F
#define HID_USAGE_F7         0x40
#define HID_USAGE_F8         0x41
#define HID_USAGE_F9         0x42
#define HID_USAGE_F10        0x43
#define HID_USAGE_F11        0x44
#define HID_USAGE_F12        0x45
#define HID_USAGE_INSERT     0x49
#define HID_USAGE_HOME       0x4A
#define HID_USAGE_PAGEUP     0x4B
#define HID_USAGE_DELETE     0x4C
#define HID_USAGE_END        0x4D
#define HID_USAGE_PAGEDOWN   0x4E
#define HID_USAGE_RIGHT      0x4F
#define HID_USAGE_LEFT       0x50
#define HID_USAGE_DOWN       0x51
#define HID_USAGE_UP         0x52

#define KB_KEY_SPECIAL_NONE      0
#define KB_KEY_SPECIAL_BACKSPACE 1
#define KB_KEY_SPECIAL_ENTER     2
#define KB_KEY_SPECIAL_SPACE     3
#define KB_KEY_SPECIAL_TAB       4
#define KB_KEY_SPECIAL_ESC       5
/* Modifier / function / navigation / arrow keys all share one
 * accent-color category; the visual style mirrors the "special"
 * button face in the upstream Rust project. */
#define KB_KEY_SPECIAL_MOD       6
#define KB_KEY_SPECIAL_FN        7
#define KB_KEY_SPECIAL_NAV       8
#define KB_KEY_SPECIAL_ARROW     9
/* UI action keys handled locally by keyboard_ui (no HID usage):
 * LANG cycles the active layout among the enabled languages;
 * MENU opens the gamepad-navigated settings menu. */
#define KB_KEY_SPECIAL_LANG      10
#define KB_KEY_SPECIAL_MENU      11

/* External tables defined in src/layout_<lang>.c. */
extern const kb_layout_t kb_layout_us;
extern const kb_layout_t kb_layout_de;
extern const kb_layout_t kb_layout_fr;
extern const kb_layout_t kb_layout_ua;

/* Active layout. Defaults to the first available (Kconfig-activated)
 * layout at boot; runtime override via kb_layout_set_active_by_name().
 * The active layout is not persisted across reboots. */
const kb_layout_t *kb_layout_active(void);
bool               kb_layout_set_active_by_name(const char *name);

/* Enumerate built-in layouts. */
int                kb_layout_count(void);
const kb_layout_t *kb_layout_by_index(int i);
const kb_layout_t *kb_layout_by_name(const char *name);
int                kb_layout_index_of(const kb_layout_t *l);

/* Compile-time availability: true when layout index i is activated
 * via its SK_LANG_ENABLE_* Kconfig switch. Only available layouts
 * are offered in the settings menu. */
bool               kb_layout_is_available(int i);

/* Enabled-language set. The on-screen settings menu lets the user
 * choose which of the built-in layouts participate in the Lng
 * (language cycle) rotation; the choice is a bitmask (bit i set =
 * layout index i enabled) persisted to NVS by keyboard_ui. At
 * least one language is always enabled, and the active layout is
 * always part of the enabled set. */
uint32_t           kb_layout_enabled_mask(void);
void               kb_layout_set_enabled_mask(uint32_t mask);
bool               kb_layout_is_enabled(int i);
void               kb_layout_set_enabled(int i, bool on);

/* Return the next enabled layout after `cur` in the rotation
 * (wraps around). Returns `cur` if it is the only enabled one. */
const kb_layout_t *kb_layout_next_enabled(const kb_layout_t *cur);

/* Convenience: bounds-checked grid lookup. Returns NULL on
 * out-of-range. */
static inline const kb_key_t *
kb_layout_key_at(const kb_layout_t *l, int row, int col)
{
    if (!l || row < 0 || row >= l->rows || col < 0 || col >= l->cols) {
        return NULL;
    }
    return &l->keys[row * l->cols + col];
}

#ifdef __cplusplus
}
#endif
