/*
 * Gamepad-event -> keyboard/HID action router.
 *
 * Button map (numbered buttons match the gamepad wire-report bit
 * positions, see components/gamepad_uart):
 *
 *   GP_BTN_0  normal keypress       / left mouse click
 *   GP_BTN_1  shifted keypress      / right mouse click
 *   GP_BTN_2  Space                 / toggle mouse scroll mode
 *   GP_BTN_3  Enter                 / (ignored in mouse mode)
 *   GP_BTN_4  Backspace             / (ignored in mouse mode)
 *   GP_BTN_5  Ctrl + selected key   / (ignored in mouse mode)
 *   GP_BTN_6  AltGr + selected key  / (ignored in mouse mode)
 *   GP_BTN_7  unused
 *   GP_BTN_8  unused
 *   GP_BTN_9  on down -> mouse mode, on up -> keyboard mode
 *
 * The D-pad / analog directions move the selection cursor (or the
 * mouse pointer in mouse mode, or the menu selection while the
 * settings menu is open).
 *
 * Mouse mode has a "scroll" sub-mode: while it is active the analog
 * axes emit wheel-scroll HID reports instead of pointer motion.
 * GP_BTN_2 toggles it; pressing the left/right mouse buttons
 */

#include "input_router.h"

#include <stdbool.h>

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "gamepad_uart.h"
#include "keyboard_ui.h"
#include "hid.h"
#include "kb_layout.h"
#include "narrator.h"
#include "sdkconfig.h"

static const char *TAG = "input_router";

/* Hold-to-repeat parameters. */
#define INITIAL_REPEAT_MS 350
#define REPEAT_INTERVAL_MS 80

/* Full-scale magnitude of a gamepad analog axis (see
 * gamepad_uart.h: signed -32767..32767). */
#define AXIS_FULL_SCALE 32767

/* HID mouse reports carry a signed 8-bit delta per axis, so a
 * single poll can move at most this many pixels. */
#define MOUSE_DELTA_MAX 127

/* True while mouse mode is in its scroll sub-mode: the analog axes
 * emit wheel-scroll reports instead of pointer motion. Always reset
 * when (re-)entering or leaving mouse mode. */
static bool s_scroll_mode;

#define SCROLL_INTERVAL_MS 200
static uint32_t last_scroll_report_time = 0;

/* Per-button state, used to drive hold-to-repeat. */
typedef struct {
    bool     down;
    uint32_t down_at_ms;
    uint32_t last_repeat_ms;
} btn_state_t;

static btn_state_t s_b[GP_BTN_COUNT];

/* current state of mouse buttons that we send in HID reports */
static uint8_t s_mouse_buttons;

static inline bool is_dir(gamepad_button_t b)
{
    return b == GP_BTN_UP || b == GP_BTN_DOWN
        || b == GP_BTN_LEFT || b == GP_BTN_RIGHT;
}

static void dir_apply(gamepad_button_t b)
{
    int dr = 0, dc = 0;
    switch (b) {
    case GP_BTN_UP:    dr = -1; break;
    case GP_BTN_DOWN:  dr =  1; break;
    case GP_BTN_LEFT:  dc = -1; break;
    case GP_BTN_RIGHT: dc =  1; break;
    default: return;
    }
    switch (keyboard_ui_get_mode()) {
    case KB_MODE_MENU:
        /* Up/Down move the cursor; Left/Right change the value. */
        if (dr) keyboard_ui_menu_move(dr);
        else    keyboard_ui_menu_adjust(dc);
        break;
    case KB_MODE_MOUSE:
        /* Mouse motion is driven by the analog axes in
         * mouse_axes_apply(), proportional to deflection; the
         * coarse N/S/E/W edge events are ignored here. */
        break;
    case KB_MODE_KEYBOARD:
    default:
        if (keyboard_ui_move(dr, dc)) {
            /* Speak the newly selected key (no-op when the
             * narrator is disabled). */
            keyboard_ui_narrate_selection();
        }
        break;
    }
}

/* Send a fixed key (Space / Enter / Backspace) and speak it. These
 * are language-neutral and not tied to the selection cursor. */
static void send_fixed(uint8_t usage)
{
    hid_send_key(0, usage);
    hid_release_all();
    narrator_speak_hid(usage);
}

/* Perform the GP_BTN_0 "action" press, optionally with an extra
 * one-shot modifier (Ctrl / AltGr) latched for the keypress. */
static void press_action(uint8_t extra_mod)
{
    switch (keyboard_ui_get_mode()) {
    case KB_MODE_MENU:
        keyboard_ui_menu_select();
        break;
    case KB_MODE_MOUSE:
        /* Pressing the left button emits a left click */
        if (!s_scroll_mode) {
            s_mouse_buttons |= HID_MS_BTN_LEFT;
            hid_send_mouse(0, 0, s_mouse_buttons, 0);
        }
        break;
    case KB_MODE_KEYBOARD:
    default:
        /* Normal keypress: applies + clears sticky modifiers and
         * narrates inside keyboard_ui_press_current(). The optional
         * one-shot modifier is consumed by that same press. */
        if (extra_mod) keyboard_ui_oneshot_mod(extra_mod);
        keyboard_ui_press_current();
        break;
    }
}

static void handle_down(gamepad_button_t b, uint32_t now)
{
    s_b[b].down = true;
    s_b[b].down_at_ms = now;
    s_b[b].last_repeat_ms = now;

    if (is_dir(b)) {
        dir_apply(b);
        return;
    }

    keyboard_ui_mode_t mode = keyboard_ui_get_mode();

    switch (b) {
    case GP_BTN_0:
        press_action(0);
        break;
    case GP_BTN_1:
        if (mode == KB_MODE_MOUSE) {
            /* Right click */
            if (!s_scroll_mode) {
                s_mouse_buttons |= HID_MS_BTN_RIGHT;
                hid_send_mouse(0, 0, s_mouse_buttons, 0);
            }
        } else if (mode == KB_MODE_KEYBOARD) {
            /* Shifted keypress: latch a one-shot Shift, then press
             * the selected key (which clears it again). */
            keyboard_ui_oneshot_mod(HID_MOD_LSHIFT);
            keyboard_ui_press_current();
        }
        break;
    case GP_BTN_2:
        if (mode == KB_MODE_MOUSE) {
            /* Toggle the wheel-scroll sub-mode. */
            s_scroll_mode = !s_scroll_mode;
            if (s_scroll_mode) {
                s_mouse_buttons = 0;
            }
        } else {
            send_fixed(HID_USAGE_SPACE);
        }
        break;
    case GP_BTN_3:
        if (mode != KB_MODE_MOUSE) send_fixed(HID_USAGE_ENTER);
        break;
    case GP_BTN_4:
        if (mode != KB_MODE_MOUSE) send_fixed(HID_USAGE_BACKSPACE);
        break;
    case GP_BTN_5:
        /* Like GP_BTN_0 but with Ctrl held for the keypress.
         * Ignored in mouse mode. */
        if (mode != KB_MODE_MOUSE) press_action(HID_MOD_LCTRL);
        break;
    case GP_BTN_6:
        /* Like GP_BTN_0 but with AltGr (right Alt) held.
         * Ignored in mouse mode. */
        if (mode != KB_MODE_MOUSE) press_action(HID_MOD_RALT);
        break;
    case GP_BTN_9:
        /* On down: enter mouse mode (scroll sub-mode off). */
        s_scroll_mode = false;
        s_mouse_buttons = 0;
        keyboard_ui_set_mode(KB_MODE_MOUSE);
        break;
    case GP_BTN_7:
    case GP_BTN_8:
    default:
        /* Unused. */
        break;
    }
}

static void handle_up(gamepad_button_t b)
{
    s_b[b].down = false;
    if (keyboard_ui_get_mode() == KB_MODE_MOUSE && (b == GP_BTN_0 || b == GP_BTN_1)) {
        /* mouse button release */
        if (b == GP_BTN_0) {
            s_mouse_buttons &= ~HID_MS_BTN_LEFT;
        }
        else {
            s_mouse_buttons &= ~HID_MS_BTN_RIGHT;
        }
        hid_send_mouse(0, 0, s_mouse_buttons, 0);
    }
    else if (b == GP_BTN_9) {
        /* On up: enter keyboard mode (scroll sub-mode off). */
        s_scroll_mode = false;
        keyboard_ui_set_mode(KB_MODE_KEYBOARD);
    }
}

static void tick_repeat(uint32_t now)
{
    for (int i = 0; i < GP_BTN_COUNT; ++i) {
        if (!s_b[i].down) continue;
        if (!is_dir((gamepad_button_t)i)) continue;
        uint32_t since_down = now - s_b[i].down_at_ms;
        if (since_down < INITIAL_REPEAT_MS) continue;
        if (now - s_b[i].last_repeat_ms < REPEAT_INTERVAL_MS) continue;
        s_b[i].last_repeat_ms = now;
        dir_apply((gamepad_button_t)i);
    }
}

/* Scale one raw analog axis value into a per-poll motion delta.
 *
 * The goal is precise control near the dead-zone and faster travel
 * as the stick is pushed: the instant the axis leaves the dead-zone
 * the cursor creeps at a fixed slow base speed (one unit per poll),
 * and from there the delta grows quadratically with deflection up to
 * `max_step` units at full deflection. The quadratic term is the
 * "acceleration": the further the stick is pushed, the faster the
 * speed climbs, while `max_step` (the selected speed level 1..7)
 * sets how aggressive that climb is. The result is clamped to the
 * signed-8-bit range a HID report can carry. */
static int axis_to_delta(int axis, int max_step)
{
    int dz = CONFIG_SK_GAMEPAD_AXIS_DEADZONE;
    int sign = 1;
    if (axis < 0) { sign = -1; axis = -axis; }
    if (axis <= dz) return 0;
    if (axis > AXIS_FULL_SCALE) axis = AXIS_FULL_SCALE;

    int span = AXIS_FULL_SCALE - dz;
    if (span <= 0) span = 1;

    /* Normalised deflection past the dead-zone, 0 < frac <= 1.
     * delta = base + (max_step - base) * frac^2, with a base of 1 so
     * motion starts immediately on leaving the dead-zone. Use 32-bit
     * float for the squared term: integer math would overflow a
     * 32-bit int (off*off alone reaches ~9.5e8 and the (max_step-1)
     * factor pushes it past INT_MAX), and `long` is only 32 bits on
     * this target. */
    int base = 1;
    if (max_step < base) max_step = base;
    float frac = (float)(axis - dz) / (float)span;   /* 0..1 */
    int delta = base + (int)((float)(max_step - base) * frac * frac);

    if (delta > MOUSE_DELTA_MAX) delta = MOUSE_DELTA_MAX;
    return sign * delta;
}

/* Read the live analog axes and, if either is outside the
 * dead-zone, emit a proportional mouse-motion report. Called
 * every poll while in mouse mode. */
static void mouse_axes_apply(uint32_t now)
{
    int16_t ax = 0, ay = 0;
    gamepad_uart_get_axes(&ax, &ay);

    int max_step = keyboard_ui_mouse_max_step();

    if (s_scroll_mode) {
        if(now >= last_scroll_report_time + SCROLL_INTERVAL_MS) {
            /* Scroll sub-mode: the axes drive wheel reports instead of
             * pointer motion, using the very same slow-start /
             * position-accelerated curve as the pointer. The HID mouse
             * report exposes a single (vertical) wheel, so the Y axis is
             * mapped to it; pushing the stick up scrolls up (wheel
             * positive = up, while the Y axis is positive downward, hence
             * the negation). */
            last_scroll_report_time = now;
            int scroll_max = max_step;
            if (scroll_max < 1) scroll_max = 1;
            int wheel = -axis_to_delta(ay, scroll_max);
            if (wheel) {
                hid_send_mouse(0, 0, 0, wheel);
            }
        }
        return;
    }

    int dx = axis_to_delta(ax, max_step);
    int dy = axis_to_delta(ay, max_step);

    if (dx || dy) {
        hid_send_mouse(dx, dy, s_mouse_buttons, 0);
    }
}

static void router_task(void *arg)
{
    QueueHandle_t q = (QueueHandle_t)arg;
    gamepad_event_t ev;

    while (1) {
        bool is_mouse_mode = (keyboard_ui_get_mode() == KB_MODE_MOUSE);

        /* Wake up at least every 20 ms so we can service
         * hold-to-repeat even while no new events arrive. */
        if (xQueueReceive(q, &ev, pdMS_TO_TICKS(20)) == pdTRUE) {
            if (!is_mouse_mode || !is_dir(ev.button)) {
                ESP_LOGD(TAG, "%s %s", gamepad_button_name(ev.button), ev.pressed ? "down" : "up");
                if (ev.pressed)
                    handle_down(ev.button, ev.time_ms);
                else
                    handle_up(ev.button);
            }
        }

        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        if( is_mouse_mode ) {
            /* Proportional pointer motion: while in mouse mode, drive
             * the cursor straight from the live analog axes every
             * poll (~20 ms) so its speed tracks the stick deflection. */
            mouse_axes_apply(now);
        }
        else {
            tick_repeat(now);
        }
    }
}

void input_router_start(QueueHandle_t events)
{
    if (!events) return;
    xTaskCreatePinnedToCore(router_task, "input_rt", 4096,
                            (void *)events, 5, NULL, 0);
}
