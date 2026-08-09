/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * AXS5106L I2C touch driver.  Hardware-verified on the
 * Waveshare ESP32-C6-Touch-LCD-1.47 board.
 *
 * The AXS5106 reports up to 2 touch points.  Register layout:
 *   0x01 : status / point count (bits 3:0)
 *   Byte offsets per point (starting at 0x02):
 *     +0   XH (bits 3:0) high nibble of x
 *     +1   XL            low  byte  of x
 *     +2   YH (bits 3:0) high nibble of y
 *     +3   YL            low  byte  of y
 *     +4,+5 unused
 */

#include "esp_lcd_touch_axs5106.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"

static const char *TAG = "axs5106";

/* Register addresses */
#define REG_TOUCH_POINTS  0x01
#define REG_P1_XH         0x03

/* The I2C device handle is stored as a module-level variable because
 * esp_lcd_touch_handle_t does not expose a field for non-standard I2C
 * handles (it is designed around esp_lcd_panel_io).  A single touch
 * controller per board is the expected usage.                        */
static i2c_master_dev_handle_t s_dev_handle;

/* Forward declarations */
static esp_err_t axs5106_read_data(esp_lcd_touch_handle_t tp);
static bool      axs5106_get_xy(esp_lcd_touch_handle_t tp,
                                 uint16_t *x, uint16_t *y,
                                 uint16_t *strength,
                                 uint8_t  *point_num,
                                 uint8_t   max_point_num);
static esp_err_t axs5106_del(esp_lcd_touch_handle_t tp);
static esp_err_t axs5106_reset(esp_lcd_touch_handle_t tp);

/* -- I2C helpers ---------------------------------------------------- */

static esp_err_t i2c_read(uint8_t reg, uint8_t *data, uint8_t len)
{
    esp_err_t ret;
    ret = i2c_master_transmit(s_dev_handle, &reg, 1, 100);
    if (ret != ESP_OK) {
        return ret;
    }
    return i2c_master_receive(s_dev_handle, data, len, 100);
}

/* -- Driver implementation ----------------------------------------- */

esp_err_t esp_lcd_touch_new_i2c_axs5106(i2c_master_dev_handle_t dev_handle,
                                         const esp_lcd_touch_config_t *config,
                                         esp_lcd_touch_handle_t *out_touch)
{
    esp_err_t            ret  = ESP_OK;
    esp_lcd_touch_t     *tp   = NULL;

    assert(config    != NULL);
    assert(out_touch != NULL);

    s_dev_handle = dev_handle;

    tp = heap_caps_calloc(1, sizeof(esp_lcd_touch_t), MALLOC_CAP_DEFAULT);
    ESP_GOTO_ON_FALSE(tp, ESP_ERR_NO_MEM, err, TAG, "no mem for AXS5106");

    tp->read_data = axs5106_read_data;
    tp->get_xy    = axs5106_get_xy;
    tp->del       = axs5106_del;
    tp->data.lock.owner = portMUX_FREE_VAL;

    memcpy(&tp->config, config, sizeof(esp_lcd_touch_config_t));

    /* INT pin -- input, falling-edge interrupt (active-low) */
    if (tp->config.int_gpio_num != GPIO_NUM_NC) {
        const gpio_config_t int_cfg = {
            .mode        = GPIO_MODE_INPUT,
            .intr_type   = (tp->config.levels.interrupt
                            ? GPIO_INTR_POSEDGE : GPIO_INTR_NEGEDGE),
            .pin_bit_mask = BIT64(tp->config.int_gpio_num),
        };
        ret = gpio_config(&int_cfg);
        ESP_GOTO_ON_ERROR(ret, err, TAG, "INT GPIO config failed");

        if (tp->config.interrupt_callback) {
            esp_lcd_touch_register_interrupt_callback(
                tp, tp->config.interrupt_callback);
        }
    }

    /* RST pin -- output */
    if (tp->config.rst_gpio_num != GPIO_NUM_NC) {
        const gpio_config_t rst_cfg = {
            .mode        = GPIO_MODE_OUTPUT,
            .pin_bit_mask = BIT64(tp->config.rst_gpio_num),
        };
        ret = gpio_config(&rst_cfg);
        ESP_GOTO_ON_ERROR(ret, err, TAG, "RST GPIO config failed");
    }

    ret = axs5106_reset(tp);
    ESP_GOTO_ON_ERROR(ret, err, TAG, "AXS5106 reset failed");

err:
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AXS5106 init failed (0x%x)", ret);
        if (tp) {
            axs5106_del(tp);
            tp = NULL;
        }
    }
    *out_touch = tp;
    return ret;
}

static esp_err_t axs5106_reset(esp_lcd_touch_handle_t tp)
{
    if (tp->config.rst_gpio_num == GPIO_NUM_NC) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(
        gpio_set_level(tp->config.rst_gpio_num, tp->config.levels.reset),
        TAG, "RST low failed");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(
        gpio_set_level(tp->config.rst_gpio_num, !tp->config.levels.reset),
        TAG, "RST high failed");
    vTaskDelay(pdMS_TO_TICKS(10));
    return ESP_OK;
}

static esp_err_t axs5106_read_data(esp_lcd_touch_handle_t tp)
{
    uint8_t buf[14] = {0};

    /* Read 14 bytes starting at REG_TOUCH_POINTS; the two 6-byte
     * coordinate records start at offset 1 within the response.    */
    esp_err_t err = i2c_read(REG_TOUCH_POINTS, buf, sizeof(buf));
    ESP_RETURN_ON_ERROR(err, TAG, "I2C read failed");

    uint8_t num_pts = buf[1] & 0x0F;
    if (num_pts == 0) {
        portENTER_CRITICAL(&tp->data.lock);
        tp->data.points = 0;
        portEXIT_CRITICAL(&tp->data.lock);
        return ESP_OK;
    }
    if (num_pts > 2) {
        num_pts = 2;
    }

    portENTER_CRITICAL(&tp->data.lock);
    tp->data.points = num_pts;
    for (uint8_t i = 0; i < num_pts; i++) {
        /* Each record is 6 bytes; the first record starts at buf[2] */
        tp->data.coords[i].x =
            (uint16_t)((buf[2 + i * 6] & 0x0F) << 8) | buf[3 + i * 6];
        tp->data.coords[i].y =
            (uint16_t)((buf[4 + i * 6] & 0x0F) << 8) | buf[5 + i * 6];
        tp->data.coords[i].strength = 1;
    }
    portEXIT_CRITICAL(&tp->data.lock);
    return ESP_OK;
}

static bool axs5106_get_xy(esp_lcd_touch_handle_t tp,
                            uint16_t *x, uint16_t *y,
                            uint16_t *strength,
                            uint8_t  *point_num,
                            uint8_t   max_point_num)
{
    portENTER_CRITICAL(&tp->data.lock);

    *point_num = (tp->data.points > max_point_num)
                 ? max_point_num : tp->data.points;

    for (uint8_t i = 0; i < *point_num; i++) {
        x[i] = tp->data.coords[i].x;
        y[i] = tp->data.coords[i].y;
        if (strength) {
            strength[i] = tp->data.coords[i].strength;
        }
    }
    tp->data.points = 0;  /* consume */

    portEXIT_CRITICAL(&tp->data.lock);
    return (*point_num > 0);
}

static esp_err_t axs5106_del(esp_lcd_touch_handle_t tp)
{
    if (tp->config.int_gpio_num != GPIO_NUM_NC) {
        gpio_reset_pin(tp->config.int_gpio_num);
        if (tp->config.interrupt_callback) {
            gpio_isr_handler_remove(tp->config.int_gpio_num);
        }
    }
    if (tp->config.rst_gpio_num != GPIO_NUM_NC) {
        gpio_reset_pin(tp->config.rst_gpio_num);
    }
    free(tp);
    return ESP_OK;
}
