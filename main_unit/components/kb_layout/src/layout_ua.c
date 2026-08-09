/*
 * Ukrainian (UA) keyboard layout -- standard JCUKEN (yi-tse-u-ka-en).
 *
 * Ported from the upstream Rust project's keymap_ua.toml. The
 * firmware sends the USB HID usage of the *physical* QWERTY key
 * position; a host configured for the Ukrainian layout then emits
 * the corresponding Cyrillic glyph. Each letter key carries the
 * Ukrainian alphabet codepoint (KG macro) so the on-screen label
 * shows the real Cyrillic letter, rendered via the 10x20 font's
 * embedded Cyrillic glyphs (see components/fonts). A short Latin
 * transliteration is kept as a fallback label for cells too small
 * for the 10x20 glyph.
 *
 * So the narrator still speaks the *Ukrainian* letter (not its
 * transliteration), each character key carries explicit narrator
 * clip tokens (sound_unshifted / sound_shifted) naming the
 * components/narrator/wav/ua_*.wav clips keyed by Unicode
 * codepoint. Special keys (Space, Enter, arrows, F-keys, ...)
 * carry no token and fall back to the language-neutral clips via
 * their HID usage.
 *
 * Grid mirrors layout_us.c (6 rows x 17 cols): function-key row
 * with the Lng / Mnu UI keys, the main JCUKEN block, a navigation
 * cluster and an arrow cluster.
 */

#include "kb_layout.h"

/* Key with explicit narrator clip tokens. */
#define KS(lu, ls, u, su, ss) { (lu), (ls), (u), KB_KEY_SPECIAL_NONE, (su), (ss), 0 }
/* Cyrillic letter key: render the Unicode glyph `cp` on screen
 * (upper-cased automatically while Shift is held) while keeping the
 * ASCII transliteration `lu` as a small-cell fallback label and
 * `su` as the narrator clip token. */
#define KG(lu, cp, u, su)    { (lu), "", (u), KB_KEY_SPECIAL_NONE, (su), NULL, (cp) }
/* Special key (accent color, no narrator token). */
#define KSP(lu, ls, u, sp)   { (lu), (ls), (u), (sp), NULL, NULL, 0 }
#define KNONE                { "",   "",   HID_USAGE_NONE, KB_KEY_SPECIAL_NONE, NULL, NULL, 0 }

#define ROWS 6
#define COLS 17

static const kb_key_t s_keys[ROWS * COLS] = {
    /* Row 0: Esc + F1..F12 (cols 0-12) + Lng + Mnu (13-14) + spacers. */
    KSP("Esc","Esc",HID_USAGE_ESCAPE,KB_KEY_SPECIAL_ESC),
    KSP("F1", "F1", HID_USAGE_F1, KB_KEY_SPECIAL_FN),
    KSP("F2", "F2", HID_USAGE_F2, KB_KEY_SPECIAL_FN),
    KSP("F3", "F3", HID_USAGE_F3, KB_KEY_SPECIAL_FN),
    KSP("F4", "F4", HID_USAGE_F4, KB_KEY_SPECIAL_FN),
    KSP("F5", "F5", HID_USAGE_F5, KB_KEY_SPECIAL_FN),
    KSP("F6", "F6", HID_USAGE_F6, KB_KEY_SPECIAL_FN),
    KSP("F7", "F7", HID_USAGE_F7, KB_KEY_SPECIAL_FN),
    KSP("F8", "F8", HID_USAGE_F8, KB_KEY_SPECIAL_FN),
    KSP("F9", "F9", HID_USAGE_F9, KB_KEY_SPECIAL_FN),
    KSP("F10","F10",HID_USAGE_F10,KB_KEY_SPECIAL_FN),
    KSP("F11","F11",HID_USAGE_F11,KB_KEY_SPECIAL_FN),
    KSP("F12","F12",HID_USAGE_F12,KB_KEY_SPECIAL_FN),
    KSP("Lng","Lng",HID_USAGE_NONE,KB_KEY_SPECIAL_LANG),
    KSP("Mnu","Mnu",HID_USAGE_NONE,KB_KEY_SPECIAL_MENU),
    KNONE, KNONE,

    /* Row 1: number row + Bksp (cols 0-13) + nav cluster (14-16).
     * Shifted glyphs follow the Ukrainian layout (e.g. Shift+3 is
     * the numero sign, shown here as the ASCII transliteration
     * "No"; Shift+grave is the hryvnia sign, not representable in
     * ASCII, so its label is blank but the narrator still speaks
     * it). */
    KS("'", "",  HID_USAGE_GRAVE, "ua_apostrophe", "ua_u20b4"),
    KS("1", "!", HID_USAGE_1,     "ua_1", "ua_u0021"),
    KS("2", "\"",HID_USAGE_2,     "ua_2", "ua_u0022"),
    KS("3", "No",HID_USAGE_3,     "ua_3", "ua_u2116"),
    KS("4", ";", HID_USAGE_4,     "ua_4", "ua_semicolon"),
    KS("5", "%", HID_USAGE_5,     "ua_5", "ua_u0025"),
    KS("6", ":", HID_USAGE_6,     "ua_6", "ua_u003a"),
    KS("7", "?", HID_USAGE_7,     "ua_7", "ua_u003f"),
    KS("8", "*", HID_USAGE_8,     "ua_8", "ua_u002a"),
    KS("9", "(", HID_USAGE_9,     "ua_9", "ua_u0028"),
    KS("0", ")", HID_USAGE_0,     "ua_0", "ua_u0029"),
    KS("-", "_", HID_USAGE_MINUS, "ua_minus", "ua_u005f"),
    KS("=", "+", HID_USAGE_EQUAL, "ua_equals", "ua_u002b"),
    KSP("Bksp","Bksp",HID_USAGE_BACKSPACE,KB_KEY_SPECIAL_BACKSPACE),
    KSP("Ins","Ins",HID_USAGE_INSERT,KB_KEY_SPECIAL_NAV),
    KSP("Hom","Hom",HID_USAGE_HOME,  KB_KEY_SPECIAL_NAV),
    KSP("PUp","PUp",HID_USAGE_PAGEUP,KB_KEY_SPECIAL_NAV),

    /* Row 2: Tab + top JCUKEN row + backslash (cols 0-13) + nav. */
    KSP("Tab","Tab",HID_USAGE_TAB,KB_KEY_SPECIAL_TAB),
    KG("j",  0x0439, HID_USAGE_Q,        "ua_u0439"),  /* j (i-kratke) */
    KG("c",  0x0446, HID_USAGE_W,        "ua_u0446"),  /* c (tse) */
    KG("u",  0x0443, HID_USAGE_E,        "ua_u0443"),  /* u */
    KG("k",  0x043a, HID_USAGE_R,        "ua_u043a"),  /* k (ka) */
    KG("e",  0x0435, HID_USAGE_T,        "ua_u0435"),  /* e */
    KG("n",  0x043d, HID_USAGE_Y,        "ua_u043d"),  /* n (en) */
    KG("g",  0x0433, HID_USAGE_U,        "ua_u0433"),  /* g (he) */
    KG("sh", 0x0448, HID_USAGE_I,        "ua_u0448"),  /* sh (sha) */
    KG("sch",0x0449, HID_USAGE_O,        "ua_u0449"),  /* sch (shcha) */
    KG("z",  0x0437, HID_USAGE_P,        "ua_u0437"),  /* z (ze) */
    KG("kh", 0x0445, HID_USAGE_LBRACKET, "ua_u0445"),  /* kh (kha) */
    KG("yi", 0x0457, HID_USAGE_RBRACKET, "ua_u0457"),  /* yi */
    KS("\\","|", HID_USAGE_BACKSLASH,"ua_backslash","ua_u007c"),
    KSP("Del","Del",HID_USAGE_DELETE,  KB_KEY_SPECIAL_NAV),
    KSP("End","End",HID_USAGE_END,     KB_KEY_SPECIAL_NAV),
    KSP("PDn","PDn",HID_USAGE_PAGEDOWN,KB_KEY_SPECIAL_NAV),

    /* Row 3: Caps + home JCUKEN row + Enter (cols 0-12), spacers. */
    KSP("Cap","Cap",HID_USAGE_CAPSLOCK,KB_KEY_SPECIAL_MOD),
    KG("f", 0x0444, HID_USAGE_A,         "ua_u0444"),  /* f (ef) */
    KG("i", 0x0456, HID_USAGE_S,         "ua_u0456"),  /* i */
    KG("v", 0x0432, HID_USAGE_D,         "ua_u0432"),  /* v (ve) */
    KG("a", 0x0430, HID_USAGE_F,         "ua_u0430"),  /* a */
    KG("p", 0x043f, HID_USAGE_G,         "ua_u043f"),  /* p (pe) */
    KG("r", 0x0440, HID_USAGE_H,         "ua_u0440"),  /* r (er) */
    KG("o", 0x043e, HID_USAGE_J,         "ua_u043e"),  /* o */
    KG("l", 0x043b, HID_USAGE_K,         "ua_u043b"),  /* l (el) */
    KG("d", 0x0434, HID_USAGE_L,         "ua_u0434"),  /* d (de) */
    KG("zh",0x0436, HID_USAGE_SEMICOLON, "ua_u0436"),  /* zh (zhe) */
    KG("ye",0x0454, HID_USAGE_APOSTROPHE,"ua_u0454"),  /* ye */
    KSP("Ent","Ent",HID_USAGE_ENTER,KB_KEY_SPECIAL_ENTER),
    KNONE, KNONE, KNONE, KNONE,

    /* Row 4: LShift + bottom JCUKEN row + RShift (cols 0-11),
     * spacers, ArrowUp at col 15. */
    KSP("Sft","Sft",HID_USAGE_NONE,KB_KEY_SPECIAL_MOD),
    KG("ya",0x044f, HID_USAGE_Z,     "ua_u044f"),  /* ya */
    KG("ch",0x0447, HID_USAGE_X,     "ua_u0447"),  /* ch (che) */
    KG("s", 0x0441, HID_USAGE_C,     "ua_u0441"),  /* s (es) */
    KG("m", 0x043c, HID_USAGE_V,     "ua_u043c"),  /* m (em) */
    KG("y", 0x0438, HID_USAGE_B,     "ua_u0438"),  /* y (y) */
    KG("t", 0x0442, HID_USAGE_N,     "ua_u0442"),  /* t (te) */
    KG("'", 0x044c, HID_USAGE_M,     "ua_u044c"),  /* soft sign */
    KG("b", 0x0431, HID_USAGE_COMMA, "ua_u0431"),  /* b (be) */
    KG("yu",0x044e, HID_USAGE_PERIOD,"ua_u044e"),  /* yu */
    KS(".", ",", HID_USAGE_SLASH,"ua_period","ua_comma"),
    KSP("Sft","Sft",HID_USAGE_NONE,KB_KEY_SPECIAL_MOD),
    KNONE, KNONE, KNONE,
    KSP("Up", "Up", HID_USAGE_UP,KB_KEY_SPECIAL_ARROW),
    KNONE,

    /* Row 5: Ctrl Win Alt + Space(x6) + AltGr RCtrl + arrow cluster.
     * Identical to the US layout: these keys are language-neutral. */
    KSP("Ctl","Ctl",HID_USAGE_NONE,KB_KEY_SPECIAL_MOD),
    KSP("Win","Win",HID_USAGE_NONE,KB_KEY_SPECIAL_MOD),
    KSP("Alt","Alt",HID_USAGE_NONE,KB_KEY_SPECIAL_MOD),
    KSP("",     "",     HID_USAGE_SPACE,KB_KEY_SPECIAL_SPACE),
    KSP("",     "",     HID_USAGE_SPACE,KB_KEY_SPECIAL_SPACE),
    KSP("Space","Space",HID_USAGE_SPACE,KB_KEY_SPECIAL_SPACE),
    KSP("",     "",     HID_USAGE_SPACE,KB_KEY_SPECIAL_SPACE),
    KSP("",     "",     HID_USAGE_SPACE,KB_KEY_SPECIAL_SPACE),
    KSP("",     "",     HID_USAGE_SPACE,KB_KEY_SPECIAL_SPACE),
    KSP("AGr","AGr",HID_USAGE_NONE,KB_KEY_SPECIAL_MOD),
    KSP("Ctl","Ctl",HID_USAGE_NONE,KB_KEY_SPECIAL_MOD),
    KNONE, KNONE, KNONE,
    KSP("Lft","Lft",HID_USAGE_LEFT, KB_KEY_SPECIAL_ARROW),
    KSP("Dn", "Dn", HID_USAGE_DOWN, KB_KEY_SPECIAL_ARROW),
    KSP("Rgt","Rgt",HID_USAGE_RIGHT,KB_KEY_SPECIAL_ARROW),
};

const kb_layout_t kb_layout_ua = {
    .name = "UA",
    .rows = ROWS,
    .cols = COLS,
    .keys = s_keys,
};
