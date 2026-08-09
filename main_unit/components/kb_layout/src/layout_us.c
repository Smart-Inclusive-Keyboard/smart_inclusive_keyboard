/*
 * US English keyboard layout.
 *
 * Mirrors the upstream Rust project's physical KEYS table
 * (clackups/smart-keyboard, src/keyboards.rs): an ortholinear
 * 6-row x 17-column grid combining a function-key row, the main
 * QWERTY block, a navigation cluster (Ins/Home/PgUp - Del/End/PgDn)
 * and an arrow cluster.
 *
 *   row 0:  Esc F1 F2 F3 F4 F5 F6 F7 F8 F9 F10 F11 F12 .  .  .  .
 *   row 1:  `  1  2  3  4  5  6  7  8  9  0  -  =  BS  Ins Home PgUp
 *   row 2:  Tab q  w  e  r  t  y  u  i  o  p  [  ]  \   Del End  PgDn
 *   row 3:  Caps a  s  d  f  g  h  j  k  l  ;  '  Ent .  .  .   .
 *   row 4:  Sft z  x  c  v  b  n  m  ,  .  /  Sft .   .   .   Up   .
 *   row 5:  Ctrl Win Alt Space Space Space Space Space Space AGr Ctl . . . Lft Dn Rgt
 *
 * Cells with no key render as no-op spacers in keyboard_ui. The
 * Space bar is repeated across six contiguous cells with the
 * accent background, so it visually reads as a single wide bar
 * within the fixed-grid renderer.
 */

#include "kb_layout.h"

#define K(lu, ls, u)         { (lu), (ls), (u), KB_KEY_SPECIAL_NONE, NULL, NULL, 0 }
#define KSP(lu, ls, u, sp)   { (lu), (ls), (u), (sp),                NULL, NULL, 0 }
#define KNONE                { "",   "",   HID_USAGE_NONE, KB_KEY_SPECIAL_NONE, NULL, NULL, 0 }

#define ROWS 6
#define COLS 17

static const kb_key_t s_keys[ROWS * COLS] = {
    /* Row 0: Esc + F1..F12 (cols 0-12), trailing spacers. */
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

    /* Row 1: number row + Bksp (cols 0-13) + nav cluster (14-16). */
    K("`","~",HID_USAGE_GRAVE), K("1","!",HID_USAGE_1), K("2","@",HID_USAGE_2),
    K("3","#",HID_USAGE_3), K("4","$",HID_USAGE_4), K("5","%",HID_USAGE_5),
    K("6","^",HID_USAGE_6), K("7","&",HID_USAGE_7), K("8","*",HID_USAGE_8),
    K("9","(",HID_USAGE_9), K("0",")",HID_USAGE_0), K("-","_",HID_USAGE_MINUS),
    K("=","+",HID_USAGE_EQUAL),
    KSP("Bksp","Bksp",HID_USAGE_BACKSPACE,KB_KEY_SPECIAL_BACKSPACE),
    KSP("Ins", "Ins", HID_USAGE_INSERT,KB_KEY_SPECIAL_NAV),
    KSP("Hom","Hom",HID_USAGE_HOME,  KB_KEY_SPECIAL_NAV),
    KSP("PUp","PUp",HID_USAGE_PAGEUP,KB_KEY_SPECIAL_NAV),

    /* Row 2: Tab + top alpha + brackets + backslash (cols 0-13) + nav (14-16). */
    KSP("Tab","Tab",HID_USAGE_TAB,KB_KEY_SPECIAL_TAB),
    K("q","Q",HID_USAGE_Q), K("w","W",HID_USAGE_W), K("e","E",HID_USAGE_E),
    K("r","R",HID_USAGE_R), K("t","T",HID_USAGE_T), K("y","Y",HID_USAGE_Y),
    K("u","U",HID_USAGE_U), K("i","I",HID_USAGE_I), K("o","O",HID_USAGE_O),
    K("p","P",HID_USAGE_P),
    K("[","{",HID_USAGE_LBRACKET), K("]","}",HID_USAGE_RBRACKET),
    K("\\","|",HID_USAGE_BACKSLASH),
    KSP("Del","Del",HID_USAGE_DELETE,  KB_KEY_SPECIAL_NAV),
    KSP("End","End",HID_USAGE_END,     KB_KEY_SPECIAL_NAV),
    KSP("PDn","PDn",HID_USAGE_PAGEDOWN,KB_KEY_SPECIAL_NAV),

    /* Row 3: Caps + home row + Enter (cols 0-12), trailing spacers. */
    KSP("Cap","Cap",HID_USAGE_CAPSLOCK,KB_KEY_SPECIAL_MOD),
    K("a","A",HID_USAGE_A), K("s","S",HID_USAGE_S), K("d","D",HID_USAGE_D),
    K("f","F",HID_USAGE_F), K("g","G",HID_USAGE_G), K("h","H",HID_USAGE_H),
    K("j","J",HID_USAGE_J), K("k","K",HID_USAGE_K), K("l","L",HID_USAGE_L),
    K(";",":",HID_USAGE_SEMICOLON), K("'","\"",HID_USAGE_APOSTROPHE),
    KSP("Ent","Ent",HID_USAGE_ENTER,KB_KEY_SPECIAL_ENTER),
    KNONE, KNONE, KNONE, KNONE,

    /* Row 4: LShift + bottom alpha + RShift (cols 0-11), spacers, ArrowUp at col 15. */
    KSP("Sft","Sft",HID_USAGE_NONE,KB_KEY_SPECIAL_MOD),
    K("z","Z",HID_USAGE_Z), K("x","X",HID_USAGE_X), K("c","C",HID_USAGE_C),
    K("v","V",HID_USAGE_V), K("b","B",HID_USAGE_B), K("n","N",HID_USAGE_N),
    K("m","M",HID_USAGE_M),
    K(",","<",HID_USAGE_COMMA), K(".",">",HID_USAGE_PERIOD),
    K("/","?",HID_USAGE_SLASH),
    KSP("Sft","Sft",HID_USAGE_NONE,KB_KEY_SPECIAL_MOD),
    KNONE, KNONE, KNONE,
    KSP("Up", "Up", HID_USAGE_UP,KB_KEY_SPECIAL_ARROW),
    KNONE,

    /* Row 5: Ctrl Win Alt + Space(x6) + AltGr RCtrl + spacers + arrow cluster. */
    KSP("Ctl","Ctl",HID_USAGE_NONE,KB_KEY_SPECIAL_MOD),
    KSP("Win","Win",HID_USAGE_NONE,KB_KEY_SPECIAL_MOD),
    KSP("Alt","Alt",HID_USAGE_NONE,KB_KEY_SPECIAL_MOD),
    /* Six contiguous Space cells form a wide space bar; label only
     * the middle cell so the bar reads as one centered glyph. */
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

const kb_layout_t kb_layout_us = {
    .name = "US",
    .rows = ROWS,
    .cols = COLS,
    .keys = s_keys,
};
