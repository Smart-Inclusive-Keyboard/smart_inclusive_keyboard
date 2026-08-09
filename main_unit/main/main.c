/*
 * esp32-smart-keyboard / main
 *
 * Boot sequence:
 *   1. nvs_flash_init()        -- needed by BLE bonding + UI prefs
 *   2. board_init()            -- panel reset, capability surface
 *   3. display_init()          -- framebuffer + ILI9341 SPI driver
 *   4. Splash screen
 *   5. keyboard_ui_init()      -- UI state, NVS-persisted prefs
 *   6. keyboard_ui_redraw_now()-- paint the virtual keyboard NOW,
 *                                 so the user sees it before the
 *                                 (potentially slow) HID / gamepad
 *                                 bring-up begins
 *   7. hid_init()              -- BLE (NimBLE) or USB (TinyUSB), per
 *                                 CONFIG_SK_HID_TRANSPORT_*
 *   8. narrator_init()         -- audio backend if available
 *   9. gamepad_uart_start()   -- receive-only UART HID link
 *  10. input_router_start()
 *  11. keyboard_ui_start_task()-- async redraw pump for subsequent
 *                                 state changes
 */

#include <stdio.h>

#include <esp_log.h>
#include <esp_err.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "board.h"
#include "display.h"
#include "theme.h"
#include "fonts.h"
#include "kb_layout.h"
#include "keyboard_ui.h"
#include "gamepad_uart.h"
#include "input_router.h"
#include "hid.h"
#include "narrator.h"
#include "audio.h"

static const char *TAG = "main";

#define FW_VERSION "0.1.0-dev"

static void splash(void)
{
    const theme_t *th = theme_active();
    int w = display_width();
    int h = display_height();
    display_clear(th->win_bg);

    const board_t *b = board_get();
    const char *line1 = "SmartKeyboard";
    char        line2[40];
    char        line3[40];
    snprintf(line2, sizeof(line2), "fw %s", FW_VERSION);
    snprintf(line3, sizeof(line3), "%s: starting", hid_transport_name());

    int s = 3;  /* 3x = 24-px tall text */
    int gw = 8 * s;
    int y = (h - 3 * gw - 2 * 6) / 2;

    int l1x = (w - (int)strlen(line1) * gw) / 2;
    int l2x = (w - (int)strlen(line2) * 8) / 2;
    int l3x = (w - (int)strlen(line3) * 8) / 2;

    display_draw_string(l1x, y,                line1, s, th->key_label, th->win_bg, true);
    display_draw_string(l2x, y + gw + 8,        line2, 1, th->status_ind_fg, th->win_bg, true);
    display_draw_string(l3x, y + gw + 8 + 16,   line3, 1, th->status_ind_fg, th->win_bg, true);

    /* Board name as a tiny footer. */
    int len = (int)strlen(b->name);
    display_draw_string((w - len * 8) / 2, h - 12, b->name, 1,
                        th->status_ind_fg, th->win_bg, true);
    display_flush();
}

static void on_hid_status(const char *text, bool connected)
{
    ESP_LOGI(TAG, "%s", text);
    keyboard_ui_set_hid_status(text, connected);
}

void app_main(void)
{
    /* 1. NVS. */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    /* 2. Board + display. */
    board_init();
    display_init();
    display_set_backlight(CONFIG_SK_DISPLAY_BACKLIGHT_PERCENT);

    /* 3. Splash. */
    splash();
    vTaskDelay(pdMS_TO_TICKS(1200));

    /* 4. UI state + initial paint of the virtual keyboard.
     *
     * We deliberately render the keyboard SYNCHRONOUSLY here, BEFORE
     * starting HID and gamepad, so the user sees a visible UI even
     * if those subsystems take noticeable time to come up (BLE
     * advertising, USB enumeration, gamepad UART bring-up can
     * each take noticeable time, and a hung peripheral would
     * otherwise leave the panel showing only the splash). The async
     * redraw task is started afterwards to handle subsequent state
     * changes. */
    keyboard_ui_init();
    keyboard_ui_redraw_now();

    /* 5. HID transport (BLE or USB, per Kconfig). */
    hid_init(on_hid_status);

    /* 6. Audio + narrator (no-ops on boards without speaker).
     *    Immediately after bring-up, play a short procedural
     *    chime so the user can hear that the speaker path is
     *    operational without waiting for the first narrated
     *    keystroke. Stubbed on boards without a speaker. */
    narrator_init();
    audio_play_startup_tune();

    /* 7. Gamepad + router. */
    QueueHandle_t q = gamepad_uart_start();
    input_router_start(q);

    /* 8. Async UI redraw pump for state changes driven by HID
     * status callbacks and gamepad input. */
    keyboard_ui_start_task();

    ESP_LOGI(TAG, "boot complete; firmware %s on %s",
             FW_VERSION, board_get()->name);
}
