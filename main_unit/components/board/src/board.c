/*
 * Common board glue. The per-board source files define the
 * board_t singleton; this file routes board_get() to it and
 * provides a default board_init() that resets the panel.
 */

#include "board.h"

#include <driver/gpio.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

extern const board_t g_board;

static const char *TAG = "board";

const board_t *board_get(void)
{
    return &g_board;
}

void board_init(void)
{
    static bool s_inited;
    if (s_inited) {
        return;
    }
    s_inited = true;

    ESP_LOGI(TAG, "Booting board: %s", g_board.name);

    /* Default RST pulse: most color-LCD controllers want at
     * least 10 ms of LOW followed by >= 120 ms of HIGH before
     * any command is accepted. The display component re-asserts
     * this in its own init too, but doing it here means the
     * panel is already past its hardware reset deassert delay
     * by the time the display task starts up. */
    if (g_board.display_type != BOARD_DISPLAY_NONE && g_board.display.rst >= 0) {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << g_board.display.rst,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io);
        gpio_set_level((gpio_num_t)g_board.display.rst, 0);
        vTaskDelay(pdMS_TO_TICKS(20));
        gpio_set_level((gpio_num_t)g_board.display.rst, 1);
        vTaskDelay(pdMS_TO_TICKS(120));
    }
}
