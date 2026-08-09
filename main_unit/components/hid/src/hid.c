/*
 * Compile-time dispatch from the unified hid_* API to the active
 * transport backend. Exactly one of SK_HID_TRANSPORT_BLE /
 * SK_HID_TRANSPORT_USB is set by Kconfig (the choice gates each
 * option on the matching SoC capability symbol), so this file
 * collapses to a thin set of forwarders at build time.
 */

#include "hid.h"

#include "sdkconfig.h"

#if CONFIG_SK_HID_TRANSPORT_BLE
#  include "ble_hid.h"
#elif CONFIG_SK_HID_TRANSPORT_USB
#  include "usb_hid.h"
#else
#  error "No HID transport selected; pick one in menuconfig (Smart Keyboard > HID interface)."
#endif

void hid_init(hid_status_cb_t cb)
{
#if CONFIG_SK_HID_TRANSPORT_BLE
    ble_hid_init((ble_status_cb_t)cb);
#else
    usb_hid_init((usb_status_cb_t)cb);
#endif
}

void hid_send_key(uint8_t modifiers, uint8_t usage)
{
#if CONFIG_SK_HID_TRANSPORT_BLE
    ble_hid_send_key(modifiers, usage);
#else
    usb_hid_send_key(modifiers, usage);
#endif
}

void hid_send_keys(uint8_t modifiers, const uint8_t *usages, int n)
{
#if CONFIG_SK_HID_TRANSPORT_BLE
    ble_hid_send_keys(modifiers, usages, n);
#else
    usb_hid_send_keys(modifiers, usages, n);
#endif
}

void hid_release_all(void)
{
#if CONFIG_SK_HID_TRANSPORT_BLE
    ble_hid_release_all();
#else
    usb_hid_release_all();
#endif
}

void hid_send_mouse(int dx, int dy, uint8_t buttons, int wheel)
{
#if CONFIG_SK_HID_TRANSPORT_BLE
    ble_hid_send_mouse(dx, dy, buttons, wheel);
#else
    usb_hid_send_mouse(dx, dy, buttons, wheel);
#endif
}

bool hid_is_connected(void)
{
#if CONFIG_SK_HID_TRANSPORT_BLE
    return ble_hid_is_connected();
#else
    return usb_hid_is_connected();
#endif
}

const char *hid_transport_name(void)
{
#if CONFIG_SK_HID_TRANSPORT_BLE
    return "BLE";
#else
    return "USB";
#endif
}
