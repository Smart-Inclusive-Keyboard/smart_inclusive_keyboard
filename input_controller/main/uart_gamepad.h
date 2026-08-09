/*
 * uart_gamepad.h -- Transmit-only UART gamepad HID transport.
 *
 * Streams a 6-byte HID gamepad report on every call to
 * uart_gamepad_report().  The report layout is identical to that used
 * by esp32s3_dual_foc_gp and consumed by esp32s3_smart_keyboard:
 *
 *   byte 0    : buttons 0-7   (bit n = button n pressed)
 *   byte 1    : buttons 8-15  (bit n = button n+8 pressed)
 *   bytes 2-3 : X axis, signed 16-bit little-endian (0 = centre)
 *   bytes 4-5 : Y axis, signed 16-bit little-endian (0 = centre)
 *
 * The UART is transmit-only (RX line unconnected).  Port and GPIO are
 * configured via CONFIG_TC_UART_TX_GPIO and CONFIG_TC_UART_BAUD.
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise UART1 for transmit-only gamepad output.
 *
 * Must be called once from app_main() before any uart_gamepad_report()
 * calls.
 *
 * @return ESP_OK on success, propagated driver error otherwise.
 */
esp_err_t uart_gamepad_init(void);

/**
 * @brief Transmit one 6-byte HID gamepad report.
 *
 * @param axis_x   X-axis value (-32767 ... +32767, 0 = centre).
 * @param axis_y   Y-axis value (-32767 ... +32767, 0 = centre).
 * @param buttons  Bitmask of pressed buttons (bit 0 = button 0, ...).
 * @return ESP_OK on success, ESP_FAIL if the write was short.
 */
esp_err_t uart_gamepad_report(int16_t axis_x, int16_t axis_y,
                               uint16_t buttons);

#ifdef __cplusplus
}
#endif
