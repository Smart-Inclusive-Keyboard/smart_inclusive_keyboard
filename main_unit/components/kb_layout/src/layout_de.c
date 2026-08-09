/*
 * German (DE) layout -- placeholder stub.
 *
 * The full DE layout is in the upstream Rust project
 * (clackups/smart-keyboard, keymap_de.toml). It uses non-ASCII
 * glyphs (umlauts, eszett) that need a UTF-8-capable font; the
 * embedded font8x8 in components/fonts only covers ASCII. Once
 * a wider font is in place, port the table over and replace
 * this placeholder.
 *
 * For now this exposes a minimal 1x1 grid so the layout choice
 * is selectable in menuconfig without crashing the UI.
 */

#include "kb_layout.h"

static const kb_key_t s_keys[1] = {
    { "DE?", "DE?", HID_USAGE_NONE, KB_KEY_SPECIAL_NONE, NULL, NULL, 0 },
};

const kb_layout_t kb_layout_de = {
    .name = "DE",
    .rows = 1,
    .cols = 1,
    .keys = s_keys,
};
