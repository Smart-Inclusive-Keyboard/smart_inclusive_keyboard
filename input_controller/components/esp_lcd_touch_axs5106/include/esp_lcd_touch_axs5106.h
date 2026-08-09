/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * AXS5106L capacitive touch controller driver for Waveshare ESP32-C6-Touch-LCD-1.47.
 * I2C address 0x63, SDA=GPIO18, SCL=GPIO19, INT=GPIO21, RST=GPIO20.
 */

#pragma once

#include "esp_lcd_touch.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create an AXS5106 touch driver instance.
 *
 * @param dev_handle   Registered I2C master device handle (address 0x63).
 * @param config       Touch panel configuration (resolution, rotation flags).
 * @param out_touch    Returned touch handle.
 * @return ESP_OK on success.
 */
esp_err_t esp_lcd_touch_new_i2c_axs5106(i2c_master_dev_handle_t dev_handle,
                                         const esp_lcd_touch_config_t *config,
                                         esp_lcd_touch_handle_t *out_touch);

/** I2C slave address of the AXS5106 controller. */
#define ESP_LCD_TOUCH_IO_I2C_AXS5106_ADDRESS  (0x63)

#ifdef __cplusplus
}
#endif
