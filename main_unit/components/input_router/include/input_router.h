#pragma once

/*
 * Gamepad-event router.
 *
 * Reads gamepad_event_t from the gamepad_uart queue, translates
 * them into UI / HID actions, and (optionally) pings the
 * narrator on selection changes.
 *
 * Button mapping (numbered buttons match the gamepad wire-report
 * bit positions):
 *   D-pad     -> keyboard_ui_move() with hold-to-repeat (or mouse
 *                / settings-menu navigation depending on mode)
 *   GP_BTN_0  -> press selected key      / left mouse click
 *   GP_BTN_1  -> Shift + selected key    / right mouse click
 *   GP_BTN_2  -> Space
 *   GP_BTN_3  -> Enter
 *   GP_BTN_4  -> Backspace
 *   GP_BTN_5  -> Ctrl + selected key  / left mouse click
 *   GP_BTN_6  -> AltGr + selected key / left mouse click
 *   GP_BTN_7  -> unused
 *   GP_BTN_8  -> unused
 *   GP_BTN_9  -> on down: keyboard mode; on up: mouse mode
 *
 * GP_BTN_5 / GP_BTN_6 behave like GP_BTN_0 but momentarily hold
 * Ctrl / AltGr for that single keypress. Sticky modifiers (Shift /
 * Ctrl / Alt / AltGr) toggled via the on-screen modifier keys stay
 * engaged until the next character key is pressed.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Spawn the router task. `events` is the queue handle returned
 * by gamepad_uart_start(). Must be called after keyboard_ui_init()
* and hid_init(). */
void input_router_start(QueueHandle_t events);

#ifdef __cplusplus
}
#endif
