/* French (FR) layout -- placeholder. See layout_de.c for context. */
#include "kb_layout.h"

static const kb_key_t s_keys[1] = {
    { "FR?", "FR?", HID_USAGE_NONE, KB_KEY_SPECIAL_NONE, NULL, NULL, 0 },
};

const kb_layout_t kb_layout_fr = {
    .name = "FR",
    .rows = 1,
    .cols = 1,
    .keys = s_keys,
};
