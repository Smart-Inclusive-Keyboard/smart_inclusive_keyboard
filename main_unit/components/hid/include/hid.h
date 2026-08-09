#pragma once

/*
 * Transport-agnostic composite HID API (keyboard + mouse).
 *
 * The firmware can act as a HID peripheral over Bluetooth Low Energy
 * *or* over USB; the backend is picked at build time via the
 * SK_HID_TRANSPORT choice in Kconfig. Both backends expose the same
 * 8-byte boot-keyboard / 4-byte mouse report layout, so the rest of
 * the firmware just calls hid_send_key() / hid_send_mouse() and is
 * unaware of which radio / bus actually carries the report.
 *
 * Report layout (identical on both backends):
 *   Keyboard (report ID 1): { modifiers, reserved, 6x usage }
 *   Mouse    (report ID 2): { buttons,  dx, dy, wheel }
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* USB HID Keyboard modifier bits. Identical to the boot-keyboard
 * modifier byte; pass any combination as the `modifiers` argument
 * of hid_send_key(). */
#define HID_MOD_LCTRL   0x01
#define HID_MOD_LSHIFT  0x02
#define HID_MOD_LALT    0x04
#define HID_MOD_LGUI    0x08
#define HID_MOD_RCTRL   0x10
#define HID_MOD_RSHIFT  0x20
#define HID_MOD_RALT    0x40
#define HID_MOD_RGUI    0x80

/* USB HID Mouse button bits. */
#define HID_MS_BTN_LEFT    0x01
#define HID_MS_BTN_RIGHT   0x02
#define HID_MS_BTN_MIDDLE  0x04

/* Connect / disconnect / pairing notifications. `text` is a short
 * human-readable line ("BLE: pairing...", "USB: mounted", ...) that
 * the UI can drop into the status bar verbatim. `connected` is true
 * once the host has fully attached (BLE: subscribed to the HID input
 * report; USB: SET_CONFIGURATION reached). */
typedef void (*hid_status_cb_t)(const char *text, bool connected);

/* Bring up the active HID transport. Must be called *after*
 * nvs_flash_init() because the BLE backend reads bonds from NVS.
 * The status callback is optional and may be invoked from a backend
 * task (NimBLE host thread for BLE, TinyUSB task for USB). */
void hid_init(hid_status_cb_t cb);

/* Send a single-key key-down report (modifiers + one usage). */
void hid_send_key(uint8_t modifiers, uint8_t usage);

/* Multi-key variant (HID 6KRO: up to 6 simultaneous usages; extras
 * are silently dropped). */
void hid_send_keys(uint8_t modifiers, const uint8_t *usages, int n);

/* Send the all-zero "no keys" report. */
void hid_release_all(void);

/* Send a mouse report. dx/dy are signed relative motion, `buttons`
 * is a HID_MS_BTN_* bitmap, `wheel` is signed vertical scroll. */
void hid_send_mouse(int dx, int dy, uint8_t buttons, int wheel);

/* True once the host has fully attached (see hid_status_cb_t). */
bool hid_is_connected(void);

/* Short label ("BLE" / "USB") describing the active transport.
 * Useful for the UI / log lines. */
const char *hid_transport_name(void);

#ifdef __cplusplus
}
#endif
