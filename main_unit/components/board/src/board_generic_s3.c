/*
 * Placeholder board file for a custom ESP32-S3 wiring.
 * Edit pin numbers to match your board, then select
 * "Generic ESP32-S3" in menuconfig under Smart Keyboard.
 */

#include "board.h"

#if CONFIG_SK_BOARD_GENERIC_ESP32S3

const board_t g_board = {
    .name = "Generic ESP32-S3 (edit me)",
    .display_type = BOARD_DISPLAY_NONE,
    .display = {
        .cs = -1, .sck = -1, .d0 = -1, .d1 = -1, .d2 = -1, .d3 = -1,
        .dc = -1, .rst = -1, .te = -1, .bl = -1, .bl_active_low = false,
        .width = 320, .height = 240, .swap_xy = false,
    },
    .gamepad_uart = {
        { .port = CONFIG_SK_GAMEPAD1_UART_PORT,
          .rx   = CONFIG_SK_GAMEPAD1_UART_RX_GPIO,
          .baud = CONFIG_SK_GAMEPAD1_UART_BAUD },
        { .port = CONFIG_SK_GAMEPAD2_UART_PORT,
          .rx   = CONFIG_SK_GAMEPAD2_UART_RX_GPIO,
          .baud = CONFIG_SK_GAMEPAD2_UART_BAUD },
    },
    .i2s = { .bclk = -1, .lrck = -1, .dout = -1, .mclk = -1, .port = 0 },
    .codec = { .i2c_port = -1, .sda = -1, .scl = -1, .addr = 0, .freq_hz = 0, .pa_pin = -1 },
    .battery_adc_channel = -1,
};

#endif
