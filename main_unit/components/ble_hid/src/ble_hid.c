/*
 * NimBLE-based composite HID (keyboard + mouse) GATT server.
 *
 * Implementation notes
 * --------------------
 *
 * Report map: a single HID device with two report IDs.
 *   ID 1 -- Keyboard: 8 bytes (modifiers | reserved | 6 usages)
 *   ID 2 -- Mouse:    4 bytes (buttons  | dx | dy | wheel)
 *
 * Advertising: General-Discoverable + Connectable, 16-bit HID
 * service UUID + complete local name from CONFIG_SK_BLE_DEVICE_NAME.
 * Restarts automatically after disconnect.
 *
 * Pairing: NimBLE "just works" with bonding (the bond table
 * persists in NVS, see CONFIG_BT_NIMBLE_NVS_PERSIST). The
 * keyboard_ui status hook surfaces connect / disconnect events.
 */

#include "ble_hid.h"

#include "sdkconfig.h"

/* The CMakeLists for this component is static (always builds this
 * TU, always PRIV_REQUIRES bt). When BLE is not the selected HID
 * transport we compile the file as an empty TU so that NimBLE
 * headers are never preprocessed and no symbols are emitted. The
 * hid.c facade only dispatches into this backend when the same
 * CONFIG is set, so the link stays clean either way. */
#if CONFIG_SK_HID_TRANSPORT_BLE

#include <string.h>

#include <esp_log.h>
#include <esp_err.h>
#include <nvs_flash.h>

#include "nimble/ble.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/ble_gap.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

/* Provided by NimBLE when CONFIG_BT_NIMBLE_NVS_PERSIST=y: installs
 * the on-flash bonding store so we don't reauthenticate every boot. */
extern void ble_store_config_init(void);

static const char *TAG = "ble_hid";

#define HID_SVC_UUID16              0x1812
#define HID_REPORT_MAP_UUID16       0x2A4B
#define HID_REPORT_UUID16           0x2A4D
#define HID_INFO_UUID16             0x2A4A
#define HID_CTRL_POINT_UUID16       0x2A4C
#define HID_PROTOCOL_MODE_UUID16    0x2A4E

#define REPORT_ID_KEYBOARD          0x01
#define REPORT_ID_MOUSE             0x02

/* USB-HID-style report descriptor. Two top-level applications
 * (Keyboard 0x06 and Mouse 0x02) each with their own report ID. */
static const uint8_t s_report_map[] = {
    /* Keyboard, Report ID 1 */
    0x05, 0x01,        /* Usage Page (Generic Desktop)              */
    0x09, 0x06,        /* Usage (Keyboard)                          */
    0xA1, 0x01,        /* Collection (Application)                  */
    0x85, REPORT_ID_KEYBOARD,
    0x05, 0x07,        /*   Usage Page (Key Codes)                  */
    0x19, 0xE0,        /*   Usage Minimum (224 -- LCtrl)            */
    0x29, 0xE7,        /*   Usage Maximum (231 -- RGUI)             */
    0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08,
    0x81, 0x02,        /*   Input (Data, Var, Abs)  - modifier bits */
    0x95, 0x01, 0x75, 0x08,
    0x81, 0x03,        /*   Input (Const)           - reserved      */
    0x95, 0x06, 0x75, 0x08,
    0x15, 0x00, 0x25, 0x65,
    0x05, 0x07,
    0x19, 0x00, 0x29, 0x65,
    0x81, 0x00,        /*   Input (Data, Array)     - 6 keys        */
    0xC0,              /* End Collection                            */

    /* Mouse, Report ID 2 */
    0x05, 0x01,        /* Usage Page (Generic Desktop)              */
    0x09, 0x02,        /* Usage (Mouse)                             */
    0xA1, 0x01,
    0x85, REPORT_ID_MOUSE,
    0x09, 0x01,        /*   Usage (Pointer)                         */
    0xA1, 0x00,        /*   Collection (Physical)                   */
    0x05, 0x09,        /*     Usage Page (Button)                   */
    0x19, 0x01, 0x29, 0x03,
    0x15, 0x00, 0x25, 0x01,
    0x95, 0x03, 0x75, 0x01,
    0x81, 0x02,        /*     Input  - 3 button bits                */
    0x95, 0x01, 0x75, 0x05,
    0x81, 0x03,        /*     Input  - 5 const padding bits         */
    0x05, 0x01,        /*     Usage Page (Generic Desktop)          */
    0x09, 0x30,        /*     Usage (X)                             */
    0x09, 0x31,        /*     Usage (Y)                             */
    0x09, 0x38,        /*     Usage (Wheel)                         */
    0x15, 0x81, 0x25, 0x7F,
    0x75, 0x08, 0x95, 0x03,
    0x81, 0x06,        /*     Input (Data, Var, Rel)                */
    0xC0,              /*   End Collection                          */
    0xC0,              /* End Collection                            */
};

/* HID Information: bcdHID = 1.11, countryCode = 0, flags = 0x02
 * (NormallyConnectable). */
static const uint8_t s_hid_info[4] = { 0x11, 0x01, 0x00, 0x02 };

/* Latest report contents (used by the read handlers + sent via
 * GATT notify when notifications are subscribed). */
static uint8_t  s_kbd_report[8];
static uint8_t  s_mouse_report[4];

static uint16_t s_conn_handle      = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_kbd_input_handle;
static uint16_t s_mouse_input_handle;

static ble_status_cb_t s_status_cb;
static uint8_t          s_addr_type;
static bool             s_connected;

static int hid_input_kbd_access(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg);
static int hid_input_mouse_access(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt, void *arg);
static int hid_report_map_access(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg);
static int hid_info_access(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg);
static int hid_ctrl_point_access(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg);

/* Report Reference descriptor (UUID 0x2908): {report ID, type=Input}. */
static int kbd_report_ref_access(uint16_t ch, uint16_t ah,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg);
static int mouse_report_ref_access(uint16_t ch, uint16_t ah,
                                   struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_dsc_def s_kbd_input_descrs[] = {
    {
        .uuid    = BLE_UUID16_DECLARE(0x2908),  /* Report Reference */
        .att_flags = BLE_ATT_F_READ,
        .access_cb = kbd_report_ref_access,
    },
    { 0 }
};

static const struct ble_gatt_dsc_def s_mouse_input_descrs[] = {
    {
        .uuid    = BLE_UUID16_DECLARE(0x2908),
        .att_flags = BLE_ATT_F_READ,
        .access_cb = mouse_report_ref_access,
    },
    { 0 }
};

static const struct ble_gatt_chr_def s_hid_chrs[] = {
    {
        .uuid       = BLE_UUID16_DECLARE(HID_REPORT_MAP_UUID16),
        .access_cb  = hid_report_map_access,
        .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC,
    },
    {
        .uuid       = BLE_UUID16_DECLARE(HID_INFO_UUID16),
        .access_cb  = hid_info_access,
        .flags      = BLE_GATT_CHR_F_READ,
    },
    {
        .uuid       = BLE_UUID16_DECLARE(HID_CTRL_POINT_UUID16),
        .access_cb  = hid_ctrl_point_access,
        .flags      = BLE_GATT_CHR_F_WRITE_NO_RSP,
    },
    /* Keyboard input report (notify) */
    {
        .uuid       = BLE_UUID16_DECLARE(HID_REPORT_UUID16),
        .access_cb  = hid_input_kbd_access,
        .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY
                    | BLE_GATT_CHR_F_READ_ENC,
        .val_handle = &s_kbd_input_handle,
        .descriptors = (struct ble_gatt_dsc_def *)s_kbd_input_descrs,
    },
    /* Mouse input report (notify) */
    {
        .uuid       = BLE_UUID16_DECLARE(HID_REPORT_UUID16),
        .access_cb  = hid_input_mouse_access,
        .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY
                    | BLE_GATT_CHR_F_READ_ENC,
        .val_handle = &s_mouse_input_handle,
        .descriptors = (struct ble_gatt_dsc_def *)s_mouse_input_descrs,
    },
    { 0 }
};

static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type        = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid        = BLE_UUID16_DECLARE(HID_SVC_UUID16),
        .characteristics = s_hid_chrs,
    },
    { 0 }
};

/* ----- GATT access handlers ----- */

static int hid_report_map_access(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_UNLIKELY;
    return os_mbuf_append(ctxt->om, s_report_map, sizeof(s_report_map))
        ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
}

static int hid_info_access(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_UNLIKELY;
    return os_mbuf_append(ctxt->om, s_hid_info, sizeof(s_hid_info))
        ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
}

static int hid_ctrl_point_access(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    /* We accept Suspend (0x00) / Exit Suspend (0x01) writes but
     * do nothing with them -- the host simply needs the
     * characteristic to exist. */
    return 0;
}

static int hid_input_kbd_access(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_UNLIKELY;
    return os_mbuf_append(ctxt->om, s_kbd_report, sizeof(s_kbd_report))
        ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
}

static int hid_input_mouse_access(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_UNLIKELY;
    return os_mbuf_append(ctxt->om, s_mouse_report, sizeof(s_mouse_report))
        ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
}

static int kbd_report_ref_access(uint16_t ch, uint16_t ah,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint8_t ref[2] = { REPORT_ID_KEYBOARD, 0x01 /* Input */ };
    return os_mbuf_append(ctxt->om, ref, sizeof(ref))
        ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
}

static int mouse_report_ref_access(uint16_t ch, uint16_t ah,
                                   struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint8_t ref[2] = { REPORT_ID_MOUSE, 0x01 /* Input */ };
    return os_mbuf_append(ctxt->om, ref, sizeof(ref))
        ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
}

/* ----- Advertising + GAP ----- */

static int ble_gap_event_cb(struct ble_gap_event *event, void *arg);

static void start_advertising(void)
{
    struct ble_hs_adv_fields fields = { 0 };
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    fields.tx_pwr_lvl_is_present = 1;

    const char *name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = (uint8_t)strlen(name);
    fields.name_is_complete = 1;

    ble_uuid16_t hid_uuid = BLE_UUID16_INIT(HID_SVC_UUID16);
    fields.uuids16 = &hid_uuid;
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;

    /* HID appearance: 0x03C1 = Keyboard. */
    fields.appearance = 0x03C1;
    fields.appearance_is_present = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_set_fields rc=%d", rc);
        return;
    }

    struct ble_gap_adv_params adv = { 0 };
    adv.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &adv,
                           ble_gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_start rc=%d", rc);
        return;
    }
    if (s_status_cb) s_status_cb("BLE: advertising", false);
}

static int ble_gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            s_connected = true;
            ESP_LOGI(TAG, "connected, handle=%u", s_conn_handle);
            ble_gap_security_initiate(s_conn_handle);
            if (s_status_cb) s_status_cb("BLE: connected", true);
        } else {
            ESP_LOGW(TAG, "connect failed status=%d", event->connect.status);
            start_advertising();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected reason=%d", event->disconnect.reason);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_connected = false;
        if (s_status_cb) s_status_cb("BLE: disconnected", false);
        start_advertising();
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "adv complete reason=%d", event->adv_complete.reason);
        start_advertising();
        break;

    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI(TAG, "encryption %s, status=%d",
                 event->enc_change.status == 0 ? "OK" : "failed",
                 event->enc_change.status);
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "subscribe attr=%u cur_notify=%d",
                 event->subscribe.attr_handle, event->subscribe.cur_notify);
        break;

    default:
        break;
    }
    return 0;
}

/* ----- NimBLE host bring-up ----- */

static void on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &s_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "infer_auto rc=%d", rc);
        return;
    }
    uint8_t addr[6];
    ble_hs_id_copy_addr(s_addr_type, addr, NULL);
    ESP_LOGI(TAG, "Device address %02x:%02x:%02x:%02x:%02x:%02x",
             addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
    start_advertising();
}

static void on_reset(int reason)
{
    ESP_LOGE(TAG, "host reset: %d", reason);
}

static void host_task(void *arg)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ble_hid_init(ble_status_cb_t cb)
{
    s_status_cb = cb;

    /* nvs_flash_init() must already have been called by app_main(). */
    if (nimble_port_init() != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed");
        return;
    }

    ble_hs_cfg.reset_cb        = on_reset;
    ble_hs_cfg.sync_cb         = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    /* SMP: bond + MITM-off ("Just Works"). */
    ble_hs_cfg.sm_io_cap     = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding    = 1;
    ble_hs_cfg.sm_mitm       = 0;
    ble_hs_cfg.sm_sc         = 1;
    ble_hs_cfg.sm_our_key_dist   = BLE_SM_PAIR_KEY_DIST_ENC
                                 | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC
                                 | BLE_SM_PAIR_KEY_DIST_ID;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(s_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "gatts_count_cfg rc=%d", rc);
        return;
    }
    rc = ble_gatts_add_svcs(s_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "gatts_add_svcs rc=%d", rc);
        return;
    }

    ble_svc_gap_device_name_set(CONFIG_SK_BLE_DEVICE_NAME);
    /* Keyboard appearance, again on the GAP service. */
    ble_svc_gap_device_appearance_set(0x03C1);

    /* Persist bonds to NVS so reconnects survive reboot. */
    ble_store_config_init();

    nimble_port_freertos_init(host_task);
}

/* ----- Outbound report helpers ----- */

static void notify(uint16_t handle, const uint8_t *data, size_t len)
{
    if (!s_connected || s_conn_handle == BLE_HS_CONN_HANDLE_NONE) return;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (!om) return;
    ble_gatts_notify_custom(s_conn_handle, handle, om);
}

void ble_hid_send_keys(uint8_t modifiers, const uint8_t *usages, int n)
{
    s_kbd_report[0] = modifiers;
    s_kbd_report[1] = 0;
    for (int i = 0; i < 6; ++i) {
        s_kbd_report[2 + i] = (i < n && usages) ? usages[i] : 0;
    }
    notify(s_kbd_input_handle, s_kbd_report, sizeof(s_kbd_report));
}

void ble_hid_send_key(uint8_t modifiers, uint8_t usage)
{
    ble_hid_send_keys(modifiers, &usage, 1);
}

void ble_hid_release_all(void)
{
    memset(s_kbd_report, 0, sizeof(s_kbd_report));
    notify(s_kbd_input_handle, s_kbd_report, sizeof(s_kbd_report));
}

void ble_hid_send_mouse(int dx, int dy, uint8_t buttons, int wheel)
{
    if (dx < -127) dx = -127;
    if (dx > 127) dx = 127;
    if (dy < -127) dy = -127;
    if (dy > 127) dy = 127;
    if (wheel < -127) wheel = -127;
    if (wheel > 127) wheel = 127;
    s_mouse_report[0] = (uint8_t)(buttons & 0x07);
    s_mouse_report[1] = (uint8_t)(int8_t)dx;
    s_mouse_report[2] = (uint8_t)(int8_t)dy;
    s_mouse_report[3] = (uint8_t)(int8_t)wheel;
    notify(s_mouse_input_handle, s_mouse_report, sizeof(s_mouse_report));
}

bool ble_hid_is_connected(void)
{
    return s_connected;
}

#endif /* CONFIG_SK_HID_TRANSPORT_BLE */

