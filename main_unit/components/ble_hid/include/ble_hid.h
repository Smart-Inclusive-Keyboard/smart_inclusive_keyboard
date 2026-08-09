#pragma once

/*
 * BLE composite HID (keyboard + mouse) over NimBLE.
 *
 * Advertises a single HID device with a Report Map containing
 * two Top-Level Collections:
 *   Report ID 1 = Keyboard (8-byte report: modifiers + reserved + 6 keys)
 *   Report ID 2 = Mouse    (4-byte report: buttons + dx + dy + wheel)
 *
 * Pairing is "Just Works" today (no passkey UI hooked up to the
 * display yet); bonds are persisted to NVS so reconnects across
 * reboots don't require re-pairing.
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* USB HID Keyboard modifier bits. Pass any combination as the
 * `modifiers` byte of ble_hid_send_key(). */
#define KB_MOD_LCTRL   0x01
#define KB_MOD_LSHIFT  0x02
#define KB_MOD_LALT    0x04
#define KB_MOD_LGUI    0x08
#define KB_MOD_RCTRL   0x10
#define KB_MOD_RSHIFT  0x20
#define KB_MOD_RALT    0x40
#define KB_MOD_RGUI    0x80

/* USB HID Mouse button bits. */
#define MS_BTN_LEFT    0x01
#define MS_BTN_RIGHT   0x02
#define MS_BTN_MIDDLE  0x04

typedef void (*ble_status_cb_t)(const char *text, bool connected);

/* Bring up the NimBLE host, register the HID GATT service, and
 * start advertising. Must be called after nvs_flash_init().
 * The status callback (optional) is invoked from the NimBLE host
 * task on every connect / disconnect / advertise event with a
 * short human-readable string. */
void ble_hid_init(ble_status_cb_t cb);

/* Send a key-down report: modifiers byte + a single HID usage.
 * If multiple keys must be pressed simultaneously, call
 * ble_hid_send_keys() instead. */
void ble_hid_send_key(uint8_t modifiers, uint8_t usage);

/* Multi-key variant; `usages` may contain up to 6 entries (HID
 * 6KRO). Extras are silently dropped. */
void ble_hid_send_keys(uint8_t modifiers, const uint8_t *usages, int n);

/* Send the all-zero "no keys" report, releasing whatever was
 * previously pressed. */
void ble_hid_release_all(void);

/* Send a mouse report. dx/dy are signed bytes (relative motion);
 * `buttons` is a MS_BTN_* bitmap; `wheel` is signed vertical
 * scroll. */
void ble_hid_send_mouse(int dx, int dy, uint8_t buttons, int wheel);

/* True after a host has connected and enabled notifications on
 * the HID input reports. */
bool ble_hid_is_connected(void);

#ifdef __cplusplus
}
#endif
