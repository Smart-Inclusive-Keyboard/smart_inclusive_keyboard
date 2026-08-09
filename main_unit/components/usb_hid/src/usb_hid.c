/*
 * USB composite HID (keyboard + mouse) backend, built on
 * espressif/esp_tinyusb. Exposes the same internal API shape as
 * components/ble_hid so the unified hid.c facade can dispatch to
 * whichever transport the SK_HID_TRANSPORT choice has selected.
 *
 * Implementation notes
 * --------------------
 *
 * Report layout (matches the BLE backend so the rest of the
 * firmware doesn't care which transport is active):
 *   Report ID 1 -- Boot keyboard: 8 bytes (modifiers | reserved | 6 usages)
 *   Report ID 2 -- Mouse:         4 bytes (buttons  | dx | dy | wheel)
 *
 * Connection model: TinyUSB's tud_mount_cb / tud_umount_cb /
 * tud_suspend_cb / tud_resume_cb weak hooks are overridden to
 * surface the host attach / detach state to the keyboard UI via
 * the status callback registered through usb_hid_init().
 *
 * Send path: tud_hid_keyboard_report() / tud_hid_mouse_report() are
 * thin TinyUSB helpers that build the 8 / 4-byte report and queue it
 * on the IN endpoint. We poll tud_hid_ready() briefly before
 * sending so callers don't have to know about endpoint pacing.
 */

#include "usb_hid.h"

#include "sdkconfig.h"

/* This translation unit is only added to SRCS when
 * CONFIG_SK_HID_TRANSPORT_USB is selected (see components/usb_hid/
 * CMakeLists.txt), so the TinyUSB headers below are guaranteed to
 * resolve and esp_tinyusb is in PRIV_REQUIRES. */

#include <string.h>

#include <esp_log.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "tinyusb.h"
#include "class/hid/hid_device.h"

static const char *TAG = "usb_hid";

#define REPORT_ID_KEYBOARD  0x01
#define REPORT_ID_MOUSE     0x02

/* USB HID report descriptor: two top-level Application collections
 * (keyboard + mouse) tagged with their respective report IDs. The
 * TinyUSB TUD_HID_REPORT_DESC_* macros emit the exact byte sequence
 * the BLE backend hand-assembles, so host parsing is identical. */
static const uint8_t s_hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(REPORT_ID_KEYBOARD)),
    TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(REPORT_ID_MOUSE)),
};

/* String descriptors. Index 0 is the supported-languages list
 * (English, 0x0409); the rest are referenced from the config
 * descriptor below by their position in this array. */
static const char *s_string_desc[] = {
    (const char[]){ 0x09, 0x04 },  /* 0: langid */
    CONFIG_SK_USB_MANUFACTURER,    /* 1: iManufacturer */
    CONFIG_SK_USB_PRODUCT,         /* 2: iProduct */
    CONFIG_SK_USB_SERIAL,          /* 3: iSerial */
    "Smart Keyboard HID",          /* 4: iInterface */
};

/* Single HID interface, IN endpoint 0x81, 16-byte max packet,
 * 10 ms polling interval (standard for boot-keyboard / mouse). */
#define EPNUM_HID       0x81
#define HID_EP_BUFSIZE  16
#define HID_POLL_MS     10

#define USB_HID_CFG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)

static const uint8_t s_configuration_desc[] = {
    /* Config descriptor: 1 interface, bus-powered, 100 mA. */
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, USB_HID_CFG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    /* HID interface: interface 0, string index 4, no boot protocol,
     * report descriptor length, EP, buffer size, poll. */
    TUD_HID_DESCRIPTOR(0, 4, HID_ITF_PROTOCOL_NONE,
                       sizeof(s_hid_report_descriptor),
                       EPNUM_HID, HID_EP_BUFSIZE, HID_POLL_MS),
};

/* Latest reports we sent. Kept around so GET_REPORT (host polling
 * the IN endpoint via the control pipe) can answer correctly. */
static uint8_t  s_kbd_report[8];     /* modifiers | reserved | 6 keys */
static uint8_t  s_mouse_report[4];   /* buttons | dx | dy | wheel */

static usb_status_cb_t s_status_cb;
static volatile bool   s_mounted;
static volatile bool   s_suspended;

static void notify(const char *text, bool connected)
{
    ESP_LOGI(TAG, "%s", text);
    if (s_status_cb) s_status_cb(text, connected);
}

/* ----- TinyUSB device-class callbacks ----- */

/* Invoked by TinyUSB to fetch the HID report descriptor. The HID
 * class driver caches the pointer, so returning a pointer to .rodata
 * is correct (the buffer must remain valid for the device lifetime). */
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return s_hid_report_descriptor;
}

/* GET_REPORT (control transfer). We only have INPUT reports;
 * answer with the most-recently-sent payload for the requested ID. */
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen)
{
    (void)instance;
    if (report_type != HID_REPORT_TYPE_INPUT) return 0;

    if (report_id == REPORT_ID_KEYBOARD) {
        uint16_t n = reqlen < sizeof(s_kbd_report) ? reqlen : sizeof(s_kbd_report);
        memcpy(buffer, s_kbd_report, n);
        return n;
    }
    if (report_id == REPORT_ID_MOUSE) {
        uint16_t n = reqlen < sizeof(s_mouse_report) ? reqlen : sizeof(s_mouse_report);
        memcpy(buffer, s_mouse_report, n);
        return n;
    }
    return 0;
}

/* SET_REPORT: the host writes us, typically OUTPUT report = keyboard
 * LED state (NumLock / CapsLock / ScrollLock). We silently accept
 * and ignore -- no LEDs on board. */
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize)
{
    (void)instance; (void)report_id; (void)report_type;
    (void)buffer;   (void)bufsize;
}

/* Bus state hooks (TinyUSB exposes these as weak symbols; defining
 * them here lets us surface attach / detach to the UI). */
void tud_mount_cb(void)
{
    s_mounted   = true;
    s_suspended = false;
    notify("USB: mounted", true);
}

void tud_umount_cb(void)
{
    s_mounted = false;
    notify("USB: unmounted", false);
}

void tud_suspend_cb(bool remote_wakeup_en)
{
    (void)remote_wakeup_en;
    s_suspended = true;
    notify("USB: suspended", false);
}

void tud_resume_cb(void)
{
    s_suspended = false;
    notify("USB: resumed", s_mounted);
}

/* ----- Send helpers ----- */

/* Block briefly for the previous IN report to drain. Boot-keyboard
 * reports are 8 bytes and the endpoint polls every HID_POLL_MS, so
 * a few milliseconds is plenty even back-to-back. */
static bool wait_ready(void)
{
    if (!s_mounted || s_suspended) return false;

    for (int i = 0; i < 20; ++i) {
        if (tud_hid_ready()) return true;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return tud_hid_ready();
}

void usb_hid_send_key(uint8_t modifiers, uint8_t usage)
{
    uint8_t keys[6] = { usage, 0, 0, 0, 0, 0 };
    usb_hid_send_keys(modifiers, keys, usage ? 1 : 0);
}

void usb_hid_send_keys(uint8_t modifiers, const uint8_t *usages, int n)
{
    memset(s_kbd_report, 0, sizeof(s_kbd_report));
    s_kbd_report[0] = modifiers;
    if (n > 6) n = 6;
    for (int i = 0; i < n; ++i) s_kbd_report[2 + i] = usages[i];

    if (!wait_ready()) return;
    /* TinyUSB helper: builds the boot-keyboard report from modifiers
     * + a uint8_t[6] keycode array. Pass the same fixed-size array
     * we just populated (less reserved+modifier prefix). */
    uint8_t keycodes[6] = {
        s_kbd_report[2], s_kbd_report[3], s_kbd_report[4],
        s_kbd_report[5], s_kbd_report[6], s_kbd_report[7],
    };
    tud_hid_keyboard_report(REPORT_ID_KEYBOARD, modifiers, keycodes);
}

void usb_hid_release_all(void)
{
    memset(s_kbd_report, 0, sizeof(s_kbd_report));
    if (!wait_ready()) return;
    tud_hid_keyboard_report(REPORT_ID_KEYBOARD, 0, NULL);
}

void usb_hid_send_mouse(int dx, int dy, uint8_t buttons, int wheel)
{
    if (dx >  127) dx =  127; else if (dx < -127) dx = -127;
    if (dy >  127) dy =  127; else if (dy < -127) dy = -127;
    if (wheel >  127) wheel =  127; else if (wheel < -127) wheel = -127;

    s_mouse_report[0] = buttons;
    s_mouse_report[1] = (uint8_t)(int8_t)dx;
    s_mouse_report[2] = (uint8_t)(int8_t)dy;
    s_mouse_report[3] = (uint8_t)(int8_t)wheel;

    if (!wait_ready()) return;
    tud_hid_mouse_report(REPORT_ID_MOUSE, buttons,
                         (int8_t)dx, (int8_t)dy,
                         (int8_t)wheel, 0 /* horizontal wheel */);
}

bool usb_hid_is_connected(void)
{
    return s_mounted && !s_suspended;
}

/* ----- Init ----- */

void usb_hid_init(usb_status_cb_t cb)
{
    s_status_cb = cb;

    const tinyusb_config_t tusb_cfg = {
        .device_descriptor        = NULL,              /* use TinyUSB default device descriptor */
        .string_descriptor        = s_string_desc,
        .string_descriptor_count  = sizeof(s_string_desc) / sizeof(s_string_desc[0]),
        .external_phy             = false,             /* on-chip USB PHY */
#if (TUD_OPT_HIGH_SPEED)
        .fs_configuration_descriptor = s_configuration_desc,
        .hs_configuration_descriptor = s_configuration_desc,
        .qualifier_descriptor        = NULL,
#else
        .configuration_descriptor    = s_configuration_desc,
#endif
    };

    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    notify("USB: starting", false);
    ESP_LOGI(TAG, "TinyUSB HID device installed (kbd+mouse)");
}
