/*
 * uart_gamepad.c -- Transmit-only UART gamepad HID transport.
 *
 * Uses UART1 with TX mapped to CONFIG_TC_UART_TX_GPIO (default GPIO13).
 * The RX line is left unconnected (UART_PIN_NO_CHANGE).
 */

#include "uart_gamepad.h"

#include "driver/uart.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "uart_gp";

#define UART_GP_PORT     UART_NUM_1

/* The driver requires an RX buffer larger than the hardware FIFO even
 * for TX-only use.  We allocate a minimal buffer that is never read.  */
#define UART_GP_RX_BUF   256

esp_err_t uart_gamepad_init(void)
{
    const uart_config_t cfg = {
        .baud_rate  = CONFIG_TC_UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    /* tx_buffer_size=0: uart_write_bytes() blocks until data enters
     * the hardware FIFO -- acceptable for our tiny 6-byte bursts.    */
    esp_err_t err = uart_driver_install(UART_GP_PORT, UART_GP_RX_BUF,
                                        0, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_param_config(UART_GP_PORT, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config: %s", esp_err_to_name(err));
        return err;
    }

    /* TX only -- leave RX/RTS/CTS unassigned. */
    err = uart_set_pin(UART_GP_PORT, CONFIG_TC_UART_TX_GPIO,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "UART1 gamepad TX=GPIO%d %d baud 8-N-1",
             CONFIG_TC_UART_TX_GPIO, CONFIG_TC_UART_BAUD);
    return ESP_OK;
}

esp_err_t uart_gamepad_report(int16_t axis_x, int16_t axis_y,
                               uint16_t buttons)
{
    const uint8_t report[6] = {
        (uint8_t)(buttons & 0xFF),          /* buttons 0-7           */
        (uint8_t)((buttons >> 8) & 0xFF),   /* buttons 8-15          */
        (uint8_t)(axis_x & 0xFF),           /* X low byte            */
        (uint8_t)((axis_x >> 8) & 0xFF),    /* X high byte           */
        (uint8_t)(axis_y & 0xFF),           /* Y low byte            */
        (uint8_t)((axis_y >> 8) & 0xFF),    /* Y high byte           */
    };

    int written = uart_write_bytes(UART_GP_PORT, report, sizeof(report));
    if (written != (int)sizeof(report)) {
        return ESP_FAIL;
    }
    return ESP_OK;
}
