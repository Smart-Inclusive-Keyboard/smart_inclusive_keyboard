#pragma once

/*
 * Virtual on-screen keyboard.
 *
 * Owns the user-visible state: selection cursor, modifier latches
 * (Shift / Ctrl / Alt), connection status text, mouse-mode flag.
 * Pure data + draw -- no peripherals touched. Driven by the
 * input_router, which translates gamepad events into the
 * keyboard_ui_handle_* calls below.
 *
 * The active layout and theme are resolved at draw time via
 * kb_layout_active() / theme_active().
 */

#include <stdint.h>
#include <stdbool.h>

#include "kb_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    KB_MODE_KEYBOARD = 0,
    KB_MODE_MOUSE,
    KB_MODE_MENU,
} keyboard_ui_mode_t;

/* Initialize after display_init() so we can size the grid to the
 * actual framebuffer. Persists the active layout / theme to NVS
 * on the next change. */
void keyboard_ui_init(void);

/* Move the selection cursor (clamped to the active layout grid).
 * Returns true if the selection actually moved. */
bool keyboard_ui_move(int drow, int dcol);

/* Toggle a sticky modifier (HID_MOD_LSHIFT / _LCTRL / _LALT from
 * hid.h). Calling with mod = 0 clears all modifiers. */
void keyboard_ui_toggle_mod(uint8_t mod);

/* Latch a one-shot modifier for the next keypress. Useful for
 * "X = Shift+key" without needing to depress Shift first. */
void keyboard_ui_oneshot_mod(uint8_t mod);

/* Press the currently selected key with the active modifier set
 * (sticky + one-shot OR'd together). Sends a key-down + key-up
 * pair to the host via hid_send_key(). */
void keyboard_ui_press_current(void);

/* Status-bar updates from outside. The strings are copied. */
void keyboard_ui_set_hid_status(const char *text, bool connected);
void keyboard_ui_set_passkey(uint32_t passkey);

/* Switch between text-entry and mouse-cursor modes. In mouse
 * mode the keyboard area is replaced with cursor velocity
 * indicators; the actual mouse-report dispatch lives in
 * input_router. */
void keyboard_ui_set_mode(keyboard_ui_mode_t mode);
keyboard_ui_mode_t keyboard_ui_get_mode(void);

/* Mark the whole screen dirty and redraw. Cheap; call after any
 * state change you want reflected on screen. */
void keyboard_ui_request_redraw(void);

/* Synchronously redraw the keyboard and flush it to the panel
 * from the calling context (no UI task required). Used at boot
 * so the keyboard is visible BEFORE long-running peripheral
 * init (HID, gamepad) starts. Safe to call before
 * keyboard_ui_start_task(). */
void keyboard_ui_redraw_now(void);

/* The UI task pump. Spawned from app_main(); takes redraw
 * requests off a queue and walks display_*. */
void keyboard_ui_start_task(void);

/* HID usage of the currently selected cell (HID_USAGE_NONE if
 * the cell is empty). Exposed for the narrator. */
int keyboard_ui_selected_hid_usage(void);

/* Currently selected key cell (NULL if out of bounds). Exposed
 * for the narrator so it can speak modifier keys (Shift / Ctrl /
 * Alt / Win / AGr) which all share HID_USAGE_NONE in the layout
 * and therefore can't be told apart by HID usage alone. */
const kb_key_t *keyboard_ui_selected_key(void);

/* Cycle to the next built-in keyboard layout (US -> DE -> FR ->
 * UA -> US). Persists the choice in NVS. */
void keyboard_ui_cycle_layout(void);

/* Cycle to the next built-in theme. Persists the choice in NVS. */
void keyboard_ui_cycle_theme(void);

/* Mouse-pointer speed factor. The setting is one of a small
 * number of discrete levels (slow .. fast); input_router reads
 * keyboard_ui_mouse_max_step() to scale the analog axis into a
 * per-poll pixel delta. The choice is persisted to NVS. */
#define KB_MOUSE_SPEED_LEVELS 7
int keyboard_ui_mouse_max_step(void);

/* True (non-zero) if the Shift modifier (sticky or one-shot) is
 * currently engaged. Exposed for the narrator so it speaks the
 * shifted glyph that would actually be sent. */
int keyboard_ui_selected_shift(void);

/* Speak the currently selected key via the narrator, taking the
 * current Shift state into account. Used by input_router on
 * navigation so the spoken name matches what a press would send. */
void keyboard_ui_narrate_selection(void);

/* Settings menu (gamepad-navigated). The menu is modal: while it
 * is open the on-screen keyboard is replaced with the settings
 * list and input_router routes navigation here. The user can pick
 * the color theme and toggle which languages take part in the Lng
 * rotation. Choices are persisted to NVS. */
void keyboard_ui_open_menu(void);
void keyboard_ui_close_menu(void);
void keyboard_ui_menu_move(int delta);     /* up/down: change selection   */
void keyboard_ui_menu_adjust(int delta);   /* left/right: change value    */
void keyboard_ui_menu_select(void);        /* activate the selected item  */

#ifdef __cplusplus
}
#endif
