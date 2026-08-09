#pragma once

/*
 * USB composite HID (keyboard + mouse) backend, built on
 * espressif/esp_tinyusb.
 *
 * Exposes the same internal API shape as components/ble_hid so the
 * unified hid.c facade can dispatch to whichever transport the
 * SK_HID_TRANSPORT choice has selected.
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*usb_status_cb_t)(const char *text, bool connected);

/* Install the TinyUSB driver, register the composite HID interface
 * (keyboard + mouse report descriptors with matching report IDs),
 * and start the device task. The callback is invoked on mount /
 * unmount / suspend / resume transitions from the TinyUSB task. */
void usb_hid_init(usb_status_cb_t cb);

void usb_hid_send_key(uint8_t modifiers, uint8_t usage);
void usb_hid_send_keys(uint8_t modifiers, const uint8_t *usages, int n);
void usb_hid_release_all(void);
void usb_hid_send_mouse(int dx, int dy, uint8_t buttons, int wheel);

/* True while the USB host has us mounted and configured. */
bool usb_hid_is_connected(void);

#ifdef __cplusplus
}
#endif
