/*
 * main.c -- ESP32 Touch Controller
 *
 * Reads capacitive touch from an AXS5106L controller, interprets the
 * gestures, and sends 6-byte HID gamepad reports over a transmit-only
 * UART to an esp32s3_smart_keyboard receiver.
 *
 * Two board types are supported, selected in menuconfig:
 *   - Waveshare ESP32-C6-Touch-LCD-1.47 (display + touch, described
 *     below).  Selects CONFIG_TC_HAS_DISPLAY and CONFIG_TC_HAS_TOUCH.
 *   - Generic ESP32 (CONFIG_TC_BOARD_GENERIC_ESP32): no display and no
 *     touch; only the pull-up button inputs and the UART gamepad output
 *     are built.  Neither capability flag is set, so all LCD/touch and
 *     gesture code below is compiled out.
 *
 * Hardware (Waveshare ESP32-C6-Touch-LCD-1.47, verified):
 *   LCD  JD9853  SPI2   SCLK=1 MOSI=2 MISO=3  CS=14 DC=15 RST=22 BL=23
 *                172x320 portrait, column gap=34, colour-inversion on
 *   Touch AXS5106L  I2C0  SDA=18 SCL=19 INT=21 RST=20  addr=0x63
 *   UART  TX=GPIO7  115200 8-N-1  (gamepad HID output)
 *   Vibration motor  GPIO4  (active high, CONFIG_TC_HAS_VIBRATION)
 *
 * Button/mode inputs (configurable, pull-up, active low):
 *   GPIO for buttons 0-15  generate game controller buttons 0-15
 *   Mode GPIO              each press toggles impulse <-> continuous mode
 *                          (touch boards only)
 *
 * Optional vibration feedback (CONFIG_TC_HAS_VIBRATION) pulses a motor
 * GPIO to acknowledge events: a very short buzz on every axis impulse HID
 * report, a short buzz on switching to VERTICAL and a slightly longer one
 * on switching to HORIZONTAL, two long buzzes on switching to continuous
 * mode and one long buzz on switching to impulse mode.  All durations are
 * configurable (see Kconfig).
 *
 * Gesture -> HID mapping
 *   Slide up        negative axis impulse (impulse mode) or proportional
 *                   axis (continuous mode)
 *   Slide down      positive axis impulse (impulse mode) or proportional
 *                   axis (continuous mode)
 *   Long tap (>=TC_LONG_TAP_MS) toggle VERTICAL / HORIZONTAL mode
 *                          (fires as soon as the threshold elapses,
 *                          without waiting for the finger to lift)
 *
 * In impulse mode a slide spanning the full length of the screen emits
 * several axis impulse events (CONFIG_TC_SLIDE_FULL_EVENTS, default 3);
 * shorter slides emit proportionally fewer events.  The slide is tracked
 * while the finger is held, so its direction can be reversed without
 * lifting: turning back on the slide emits impulses in the new direction.
 *
 * The finger always slides vertically (up/down).  In continuous mode
 * button 9 is held permanently on and the active axis is set proportional
 * to the sliding finger's distance from the middle of the touch screen;
 * the long tap switches the active axis just as in impulse mode.  Both
 * VERTICAL and HORIZONTAL modes react to the same up/down gesture.
 *
 * In VERTICAL mode the axis output is sent on the Y axis; in HORIZONTAL
 * mode it is sent on the X axis.
 *
 * The screen shows a schematic navigation guide with two arrows and a
 * short centre divider line.  The current mode is indicated by a bright
 * edge line: along the right edge in VERTICAL mode and along the top edge
 * in HORIZONTAL mode.  All on-screen elements (arrows, divider and edge
 * lines) share one colour and switch together between an idle and an
 * active colour: in impulse mode the idle CONFIG_TC_COLOR_IMPULSE_IDLE
 * and the active CONFIG_TC_COLOR_IMPULSE_ACTIVE (applied as soon as a
 * slide is detected), and in continuous mode the indicator colour pair
 * (idle CONFIG_TC_COLOR_CONTINUOUS_IDLE, active
 * CONFIG_TC_COLOR_CONTINUOUS_ACTIVE).  Backlight brightness and the
 * interface colours are configurable (see Kconfig).
 */

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "driver/gpio.h"

#include "sdkconfig.h"

#if CONFIG_TC_HAS_DISPLAY
#include "driver/spi_master.h"
#include "driver/ledc.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_jd9853.h"

#include "esp_lvgl_port.h"
#include "lvgl.h"
#endif /* CONFIG_TC_HAS_DISPLAY */

#if CONFIG_TC_HAS_TOUCH
#include "driver/i2c_master.h"

#include "esp_lcd_touch.h"
#include "esp_lcd_touch_axs5106.h"
#endif /* CONFIG_TC_HAS_TOUCH */

#include "uart_gamepad.h"

static const char *TAG = "tc";

/* ---------------------------------------------------------------------
 * Board-specific pin definitions
 * --------------------------------------------------------------------- */
#ifdef CONFIG_TC_BOARD_WAVESHARE_C6_TOUCH_LCD_147

/* LCD SPI (SPI2) */
#define LCD_SCLK         GPIO_NUM_1
#define LCD_MOSI         GPIO_NUM_2
#define LCD_MISO         GPIO_NUM_3
#define LCD_CS           GPIO_NUM_14
#define LCD_DC           GPIO_NUM_15
#define LCD_RST          GPIO_NUM_22
#define LCD_BL           GPIO_NUM_23
#define LCD_PIXEL_CLK_HZ (80 * 1000 * 1000)
#define LCD_H_RES        172
#define LCD_V_RES        320
#define LCD_COL_GAP      34   /* JD9853 x-offset at rotation 0 */

/* Touch I2C (I2C0) */
#define TP_SDA           GPIO_NUM_18
#define TP_SCL           GPIO_NUM_19
#define TP_INT           GPIO_NUM_21
#define TP_RST           GPIO_NUM_20

/* Backlight PWM */
#define BL_LEDC_TIMER    LEDC_TIMER_0
#define BL_LEDC_MODE     LEDC_LOW_SPEED_MODE
#define BL_LEDC_CH       LEDC_CHANNEL_0
#define BL_LEDC_DUTY_RES LEDC_TIMER_10_BIT
#define BL_LEDC_FREQ_HZ  5000

#endif /* CONFIG_TC_BOARD_WAVESHARE_C6_TOUCH_LCD_147 */

/* ---------------------------------------------------------------------
 * Application constants
 * --------------------------------------------------------------------- */
#define INPUT_POLL_MS   20    /* button / touch polling interval      */

/* Button input GPIOs (pull-up, active low, from Kconfig). */
#define BTN_GPIO_COUNT  16

/* Button index that is held on permanently while in continuous mode. */
#define CONTINUOUS_BTN  9

#if CONFIG_TC_HAS_DISPLAY
#define DRAW_BUF_LINES  50    /* LVGL DMA draw buffer height in lines */

/* Interface colours (24-bit 0xRRGGBB, from Kconfig). */
#define COLOR_BG        lv_color_hex(CONFIG_TC_COLOR_BACKGROUND)
#define COLOR_FG        lv_color_hex(CONFIG_TC_COLOR_IMPULSE_IDLE)
#define COLOR_HL        lv_color_hex(CONFIG_TC_COLOR_IMPULSE_ACTIVE)
#define COLOR_CONT_IDLE   lv_color_hex(CONFIG_TC_COLOR_CONTINUOUS_IDLE)
#define COLOR_CONT_ACTIVE lv_color_hex(CONFIG_TC_COLOR_CONTINUOUS_ACTIVE)

/* vertical/horizontal indicator line along the edge */
#define EDGE_INDICATOR_THICKNESS 4

/* Width of the short centre divider line -- approximately the width of
 * the up/down arrow glyphs it sits between. */
#define DIVIDER_WIDTH   24
#endif /* CONFIG_TC_HAS_DISPLAY */

#if CONFIG_TC_HAS_TOUCH
#define IMPULSE_MS      100   /* axis impulse duration                */

#define SLIDE_MIN_PX    CONFIG_TC_SLIDE_MIN_PX
#define TAP_MAX_MOVE_PX CONFIG_TC_TAP_MAX_MOVE_PX
#define LONG_TAP_MS     CONFIG_TC_LONG_TAP_MS
#define SLIDE_FULL_EVENTS CONFIG_TC_SLIDE_FULL_EVENTS

#define MODE_GPIO       CONFIG_TC_MODE_GPIO
#endif /* CONFIG_TC_HAS_TOUCH */

#if CONFIG_TC_HAS_VIBRATION
/* Vibration motor GPIO (active high) and feedback pulse durations. */
#define VIB_GPIO          CONFIG_TC_VIBRATION_GPIO
#define VIB_IMPULSE_MS    CONFIG_TC_VIB_IMPULSE_MS    /* very short */
#define VIB_VERTICAL_MS   CONFIG_TC_VIB_VERTICAL_MS   /* short      */
#define VIB_HORIZONTAL_MS CONFIG_TC_VIB_HORIZONTAL_MS /* longer     */
#define VIB_LONG_MS       CONFIG_TC_VIB_LONG_MS       /* long       */
#define VIB_GAP_MS        CONFIG_TC_VIB_GAP_MS        /* pulse gap  */
#else
/* Fallback durations so the vibration_trigger() no-op call sites compile
 * unchanged when no vibration motor is present. */
#define VIB_IMPULSE_MS    0
#define VIB_VERTICAL_MS   0
#define VIB_HORIZONTAL_MS 0
#define VIB_LONG_MS       0
#define VIB_GAP_MS        0
#endif /* CONFIG_TC_HAS_VIBRATION */

typedef enum {
    MODE_VERTICAL,
    MODE_HORIZONTAL,
} tc_mode_t;

/* Axis output mode, selected by the mode GPIO. */
typedef enum {
    OUTPUT_IMPULSE,
    OUTPUT_CONTINUOUS,
} tc_output_mode_t;

/* ---------------------------------------------------------------------
 * Module-level state
 * --------------------------------------------------------------------- */
#if CONFIG_TC_HAS_DISPLAY
static esp_lcd_panel_io_handle_t s_io_handle;
static esp_lcd_panel_handle_t    s_panel;
static lv_disp_t                *s_disp;
static lv_obj_t                 *s_line_right;  /* shown in VERTICAL mode   */
static lv_obj_t                 *s_line_top;    /* shown in HORIZONTAL mode */
static lv_obj_t                 *s_lbl_up;      /* up arrow                 */
static lv_obj_t                 *s_lbl_dn;      /* down arrow               */
static lv_obj_t                 *s_divider;     /* short centre line        */
#endif /* CONFIG_TC_HAS_DISPLAY */
#if CONFIG_TC_HAS_TOUCH
static esp_lcd_touch_handle_t    s_touch;
#endif /* CONFIG_TC_HAS_TOUCH */
#if CONFIG_TC_HAS_DISPLAY || CONFIG_TC_HAS_TOUCH
static volatile tc_mode_t        s_mode = MODE_VERTICAL;
#endif
static volatile tc_output_mode_t s_output_mode = OUTPUT_IMPULSE;

/* Button GPIOs (buttons 0-15; -1 = unassigned). */
static const int s_btn_gpios[BTN_GPIO_COUNT] = {
    CONFIG_TC_BTN0_GPIO,  CONFIG_TC_BTN1_GPIO,  CONFIG_TC_BTN2_GPIO,
    CONFIG_TC_BTN3_GPIO,  CONFIG_TC_BTN4_GPIO,  CONFIG_TC_BTN5_GPIO,
    CONFIG_TC_BTN6_GPIO,  CONFIG_TC_BTN7_GPIO,  CONFIG_TC_BTN8_GPIO,
    CONFIG_TC_BTN9_GPIO,  CONFIG_TC_BTN10_GPIO, CONFIG_TC_BTN11_GPIO,
    CONFIG_TC_BTN12_GPIO, CONFIG_TC_BTN13_GPIO, CONFIG_TC_BTN14_GPIO,
    CONFIG_TC_BTN15_GPIO,
};

/* Latest button bitmask read from the button GPIOs (buttons 0-15). */
static volatile uint16_t s_gpio_buttons;

/* Latest axis values pushed to the gamepad report. */
static volatile int16_t s_axis_x;
static volatile int16_t s_axis_y;

/* ---------------------------------------------------------------------
 * Hardware initialisation
 * --------------------------------------------------------------------- */
#if CONFIG_TC_HAS_DISPLAY
static void hw_lcd_init(void)
{
    /* SPI bus */
    const spi_bus_config_t buscfg = {
        .sclk_io_num     = LCD_SCLK,
        .mosi_io_num     = LCD_MOSI,
        .miso_io_num     = LCD_MISO,
        .quadhd_io_num   = -1,
        .quadwp_io_num   = -1,
        .max_transfer_sz = LCD_H_RES * DRAW_BUF_LINES * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    /* Panel IO */
    esp_lcd_panel_io_spi_config_t io_cfg =
        JD9853_PANEL_IO_SPI_CONFIG(LCD_CS, LCD_DC, NULL, NULL);
    io_cfg.pclk_hz = LCD_PIXEL_CLK_HZ;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_cfg, &s_io_handle));

    /* Panel */
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = LCD_RST,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_jd9853(s_io_handle, &panel_cfg, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(s_panel, LCD_COL_GAP, 0));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel, false, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    /* Backlight -- brightness from CONFIG_TC_SCREEN_BRIGHTNESS (percent) */
    const ledc_timer_config_t ledc_timer = {
        .speed_mode      = BL_LEDC_MODE,
        .timer_num       = BL_LEDC_TIMER,
        .duty_resolution = BL_LEDC_DUTY_RES,
        .freq_hz         = BL_LEDC_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    const uint32_t bl_max_duty = (1u << BL_LEDC_DUTY_RES) - 1u;
    const uint32_t bl_duty =
        (bl_max_duty * (uint32_t)CONFIG_TC_SCREEN_BRIGHTNESS) / 100u;

    const ledc_channel_config_t ledc_ch = {
        .speed_mode = BL_LEDC_MODE,
        .channel    = BL_LEDC_CH,
        .timer_sel  = BL_LEDC_TIMER,
        .intr_type  = LEDC_INTR_DISABLE,
        .gpio_num   = LCD_BL,
        .duty       = bl_duty,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_ch));
}
#endif /* CONFIG_TC_HAS_DISPLAY */

#if CONFIG_TC_HAS_TOUCH
static void hw_touch_init(void)
{
    /* I2C master bus */
    i2c_master_bus_handle_t i2c_bus;
    const i2c_master_bus_config_t i2c_cfg = {
        .clk_source                   = I2C_CLK_SRC_DEFAULT,
        .i2c_port                     = I2C_NUM_0,
        .scl_io_num                   = TP_SCL,
        .sda_io_num                   = TP_SDA,
        .glitch_ignore_cnt            = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_cfg, &i2c_bus));

    /* Add AXS5106 device */
    i2c_master_dev_handle_t dev;
    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = ESP_LCD_TOUCH_IO_I2C_AXS5106_ADDRESS,
        .scl_speed_hz    = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &dev_cfg, &dev));

    /* Touch configuration for portrait rotation 0:
     *   mirror_x=1 maps raw x so that x=0 is at the left edge on screen. */
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max        = LCD_H_RES,
        .y_max        = LCD_V_RES,
        .rst_gpio_num = TP_RST,
        .int_gpio_num = TP_INT,
        .levels = {
            .reset     = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy  = 0,
            .mirror_x = 1,
            .mirror_y = 0,
        },
    };

    /* Retry -- controller may be slow to wake after power-on. */
    for (int attempt = 1; attempt <= 6; attempt++) {
        esp_err_t err = esp_lcd_touch_new_i2c_axs5106(dev, &tp_cfg, &s_touch);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "AXS5106 ready (attempt %d)", attempt);
            return;
        }
        ESP_LOGW(TAG, "AXS5106 attempt %d: %s", attempt, esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(80));
    }
    s_touch = NULL;
    ESP_LOGW(TAG, "Touch unavailable -- gesture input disabled");
}
#endif /* CONFIG_TC_HAS_TOUCH */

/* Configure the button (and, on touch boards, mode) GPIOs as pull-up
 * inputs. */
static void hw_buttons_init(void)
{
    uint64_t pin_mask = 0;
    for (int i = 0; i < BTN_GPIO_COUNT; i++) {
        if (s_btn_gpios[i] >= 0) {
            pin_mask |= 1ULL << s_btn_gpios[i];
        }
    }
#if CONFIG_TC_HAS_TOUCH
    pin_mask |= 1ULL << MODE_GPIO;
#endif

    const gpio_config_t cfg = {
        .pin_bit_mask = pin_mask,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));

    char btn_list[160];
    int  pos = 0;
    for (int i = 0; i < BTN_GPIO_COUNT; i++) {
        int remaining = (int)sizeof(btn_list) - pos;
        if (remaining <= 0) {
            break;
        }
        int n;
        if (s_btn_gpios[i] >= 0) {
            n = snprintf(btn_list + pos, (size_t)remaining,
                         "%s%d", (i == 0) ? "" : ",", s_btn_gpios[i]);
        } else {
            n = snprintf(btn_list + pos, (size_t)remaining,
                         "%soff", (i == 0) ? "" : ",");
        }
        if (n < 0 || n >= remaining) {
            break;  /* encoding error or output truncated */
        }
        pos += n;
    }
#if CONFIG_TC_HAS_TOUCH
    ESP_LOGI(TAG, "Buttons 0-15 GPIO %s  mode GPIO %d", btn_list, MODE_GPIO);
#else
    ESP_LOGI(TAG, "Buttons 0-15 GPIO %s", btn_list);
#endif
}

/* ---------------------------------------------------------------------
 * Vibration motor haptic feedback
 *
 * A dedicated task drains a queue of vibration patterns so the event
 * producers (touch task) never block on the motor timing.  Each pattern
 * pulses the motor GPIO "count" times for "on_ms" milliseconds, with an
 * idle "gap_ms" between consecutive pulses.
 * --------------------------------------------------------------------- */
#if CONFIG_TC_HAS_VIBRATION
typedef struct {
    uint16_t on_ms;
    uint16_t gap_ms;
    uint8_t  count;
} vib_pattern_t;

static QueueHandle_t s_vib_queue;

/* Queue a vibration pattern; drops silently if the queue is full or the
 * subsystem has not been initialised yet. */
static void vibration_trigger(uint16_t on_ms, uint16_t gap_ms, uint8_t count)
{
    if (!s_vib_queue) {
        return;
    }
    vib_pattern_t p = { .on_ms = on_ms, .gap_ms = gap_ms, .count = count };
    xQueueSend(s_vib_queue, &p, 0);
}

static void vibration_task(void *arg)
{
    (void)arg;
    vib_pattern_t p;
    while (1) {
        if (xQueueReceive(s_vib_queue, &p, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        for (uint8_t i = 0; i < p.count; i++) {
            gpio_set_level(VIB_GPIO, 1);
            vTaskDelay(pdMS_TO_TICKS(p.on_ms));
            gpio_set_level(VIB_GPIO, 0);
            if (i + 1 < p.count) {
                vTaskDelay(pdMS_TO_TICKS(p.gap_ms));
            }
        }
    }
}

/* Configure the vibration motor GPIO as an output (driven low) and start
 * the vibration task. */
static void hw_vibration_init(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << VIB_GPIO,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    gpio_set_level(VIB_GPIO, 0);

    s_vib_queue = xQueueCreate(8, sizeof(vib_pattern_t));
    ESP_ERROR_CHECK(s_vib_queue ? ESP_OK : ESP_ERR_NO_MEM);
    BaseType_t task_ok =
        xTaskCreate(vibration_task, "vibration", 2048, NULL, 4, NULL);
    ESP_ERROR_CHECK(task_ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_LOGI(TAG, "Vibration motor GPIO %d", VIB_GPIO);
}
#else
static inline void vibration_trigger(uint16_t on_ms, uint16_t gap_ms,
                                     uint8_t count)
{
    (void)on_ms;
    (void)gap_ms;
    (void)count;
}
#endif /* CONFIG_TC_HAS_VIBRATION */

#if CONFIG_TC_HAS_DISPLAY
/* ---------------------------------------------------------------------
 * LVGL setup
 * --------------------------------------------------------------------- */
static void lvgl_setup(void)
{
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority     = 4,
        .task_stack        = 10 * 1024,
        .task_affinity     = -1,
        .task_max_sleep_ms = 500,
        .timer_period_ms   = 5,
    };
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle     = s_io_handle,
        .panel_handle  = s_panel,
        .buffer_size   = LCD_H_RES * DRAW_BUF_LINES,
        .double_buffer = true,
        .hres          = LCD_H_RES,
        .vres          = LCD_V_RES,
        .monochrome    = false,
        .rotation = {
            .swap_xy  = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma  = true,
#if LVGL_VERSION_MAJOR >= 9
            .swap_bytes = true,
#endif
        },
    };
    s_disp = lvgl_port_add_disp(&disp_cfg);
}

/* ---------------------------------------------------------------------
 * Navigation guide UI (schematic style: black background, thin lines)
 * --------------------------------------------------------------------- */

/*
 * Screen layout (172 x 320 portrait):
 *
 *  +-----------------------------+  y=0     top edge line = HORIZONTAL mode
 *  |            ^                |       up-arrow  (slide = negative axis)
 *  |                             |
 *  |                             ||      right edge line = VERTICAL mode
 *  |            ---              |  y=160  short centre divider
 *  |                             ||
 *  |                             |
 *  |            v                |       down-arrow (slide = positive axis)
 *  +-----------------------------+  y=320
 *
 * The current mode is shown by a bright edge line: right edge in
 * VERTICAL mode, top edge in HORIZONTAL mode.  Game controller buttons
 * come from dedicated GPIO inputs, not from on-screen taps.
 */
static void ui_update_mode(tc_mode_t mode);
static void ui_refresh_edges(void);

static void ui_create(void)
{
    lv_obj_t *scr = lv_scr_act();

    /* Configurable background */
    lv_obj_set_style_bg_color(scr, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* Shared style for thin foreground lines */
    static lv_style_t st_line;
    lv_style_init(&st_line);
    lv_style_set_line_color(&st_line, COLOR_FG);
    lv_style_set_line_width(&st_line, 1);
    lv_style_set_line_opa(&st_line, LV_OPA_COVER);

    /* Thicker style for the bright mode-indicator edge line */
    static lv_style_t st_edge;
    lv_style_init(&st_edge);
    lv_style_set_line_color(&st_edge, COLOR_FG);
    lv_style_set_line_width(&st_edge, EDGE_INDICATOR_THICKNESS);
    lv_style_set_line_opa(&st_edge, LV_OPA_COVER);

    /* -- Up arrow -- top of screen -- */
    s_lbl_up = lv_label_create(scr);
    lv_label_set_text(s_lbl_up, LV_SYMBOL_UP);
    lv_obj_set_style_text_color(s_lbl_up, COLOR_FG, 0);
    lv_obj_align(s_lbl_up, LV_ALIGN_TOP_MID, 0, 10);

    /* -- Centre divider line -- a short segment, roughly the width of the
     * arrow glyphs, centred horizontally at the screen middle. */
    static lv_point_t div_pts[2] = {{0, 0}, {DIVIDER_WIDTH, 0}};
    s_divider = lv_line_create(scr);
    lv_obj_add_style(s_divider, &st_line, 0);
    lv_line_set_points(s_divider, div_pts, 2);
    /* Place so the line sits centred at y=160 (screen centre) */
    lv_obj_set_pos(s_divider, (LCD_H_RES - DIVIDER_WIDTH) / 2, LCD_V_RES / 2);

    /* -- Down arrow -- bottom area -- */
    s_lbl_dn = lv_label_create(scr);
    lv_label_set_text(s_lbl_dn, LV_SYMBOL_DOWN);
    lv_obj_set_style_text_color(s_lbl_dn, COLOR_FG, 0);
    lv_obj_align(s_lbl_dn, LV_ALIGN_BOTTOM_MID, 0, -10);

    /* -- Mode indicator: line along the right edge (VERTICAL mode) -- */
    static lv_point_t right_pts[2] = {{0, 0}, {0, LCD_V_RES - EDGE_INDICATOR_THICKNESS}};
    s_line_right = lv_line_create(scr);
    lv_obj_add_style(s_line_right, &st_edge, 0);
    lv_line_set_points(s_line_right, right_pts, 2);
    lv_obj_set_pos(s_line_right, LCD_H_RES - EDGE_INDICATOR_THICKNESS, 0);

    /* -- Mode indicator: line along the top edge (HORIZONTAL mode) -- */
    static lv_point_t top_pts[2] = {{0, 0}, {LCD_H_RES - EDGE_INDICATOR_THICKNESS, 0}};
    s_line_top = lv_line_create(scr);
    lv_obj_add_style(s_line_top, &st_edge, 0);
    lv_line_set_points(s_line_top, top_pts, 2);
    lv_obj_set_pos(s_line_top, 0, 0);

    /* Initial mode is VERTICAL: show right edge, hide top edge, and
     * paint the edge lines with the current output mode's base colour. */
    ui_update_mode(s_mode);
    ui_refresh_edges();
}

static void ui_update_mode(tc_mode_t mode)
{
    /* Must be called with the LVGL port lock held. */
    if (mode == MODE_VERTICAL) {
        lv_obj_clear_flag(s_line_right, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_line_top, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_line_right, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_line_top, LV_OBJ_FLAG_HIDDEN);
    }
}

/* Base colour of the interface when no gesture is active: the idle
 * colour for the current output mode (impulse or continuous). */
static lv_color_t edge_base_color(void)
{
    return (s_output_mode == OUTPUT_CONTINUOUS) ? COLOR_CONT_IDLE : COLOR_FG;
}

/* Colour of the interface while a slide gesture is in progress: the
 * active colour for the current output mode (impulse or continuous). */
static lv_color_t edge_active_color(void)
{
    return (s_output_mode == OUTPUT_CONTINUOUS) ? COLOR_CONT_ACTIVE : COLOR_HL;
}

/* Repaint every on-screen element (edge lines, arrows and the centre
 * divider) with a single colour so they always change together. */
static void ui_set_colors(lv_color_t c)
{
    if (lvgl_port_lock(pdMS_TO_TICKS(100))) {
        lv_obj_set_style_line_color(s_line_right, c, 0);
        lv_obj_set_style_line_color(s_line_top, c, 0);
        lv_obj_set_style_line_color(s_divider, c, 0);
        lv_obj_set_style_text_color(s_lbl_up, c, 0);
        lv_obj_set_style_text_color(s_lbl_dn, c, 0);
        lvgl_port_unlock();
    }
}

/* Switch all on-screen elements to the active or idle colour at once. */
static void ui_set_active(bool on)
{
    ui_set_colors(on ? edge_active_color() : edge_base_color());
}

/* Repaint every on-screen element with the current output mode's base
 * (idle) colour. */
static void ui_refresh_edges(void)
{
    ui_set_colors(edge_base_color());
}
#endif /* CONFIG_TC_HAS_DISPLAY */

/* ---------------------------------------------------------------------
 * Gamepad event dispatch helpers
 * --------------------------------------------------------------------- */

/* Send a report with the current axis values, merging the button GPIO
 * bitmask (buttons 0-15) and, in continuous mode, the always-on button. */
static void gamepad_flush(void)
{
    uint16_t buttons = s_gpio_buttons;
    if (s_output_mode == OUTPUT_CONTINUOUS) {
        buttons |= (uint16_t)(1u << CONTINUOUS_BTN);
    }
    uart_gamepad_report(s_axis_x, s_axis_y, buttons);
}

#if CONFIG_TC_HAS_TOUCH
/* Send an impulse on the active axis: max value for IMPULSE_MS, then 0. */
static void send_axis_impulse(int16_t value)
{
    vibration_trigger(VIB_IMPULSE_MS, 0, 1);
    if (s_mode == MODE_VERTICAL) {
        s_axis_x = 0;
        s_axis_y = value;
    } else {
        s_axis_x = value;
        s_axis_y = 0;
    }
    gamepad_flush();
    vTaskDelay(pdMS_TO_TICKS(IMPULSE_MS));
    s_axis_x = 0;
    s_axis_y = 0;
    gamepad_flush();
}

/*
 * Emit a run of axis impulses on the active axis.  A slide that covers
 * the full length of the screen produces SLIDE_FULL_EVENTS impulses;
 * shorter slides produce proportionally fewer (always at least one).
 * The consecutive impulses are separated by an idle gap so the receiver
 * registers them as distinct navigation steps.
 */
static void send_axis_impulses(int16_t value, int span_px, int full_px)
{
    int count = 1;
    if (full_px > 0) {
        count = (span_px * SLIDE_FULL_EVENTS) / full_px;
    }
    if (count < 1) {
        count = 1;
    }
    if (count > SLIDE_FULL_EVENTS) {
        count = SLIDE_FULL_EVENTS;
    }

    for (int i = 0; i < count; i++) {
        send_axis_impulse(value);
        if (i + 1 < count) {
            vTaskDelay(pdMS_TO_TICKS(IMPULSE_MS));
        }
    }
}

/*
 * Emit the axis impulses for a single vertical slide segment that runs
 * from from_y to to_y.  Slides shorter than SLIDE_MIN_PX are ignored.
 * The interface is switched to the active colour by the caller as soon
 * as the slide is detected, so no highlighting happens here.
 */
static void impulse_slide(int from_y, int to_y)
{
    int d  = to_y - from_y;
    int ad = d < 0 ? -d : d;
    if (ad < SLIDE_MIN_PX) {
        return;
    }
    /* Slide up (to_y < from_y) drives the negative axis, slide down the
     * positive axis, matching the on-screen up/down arrows. */
    send_axis_impulses(d < 0 ? -32767 : 32767, ad, LCD_V_RES);
}

/*
 * Map a finger position to a signed axis value proportional to its
 * distance from the middle of the span (half of the screen dimension).
 * The result is clamped to the valid +-32767 range.
 */
static int16_t continuous_axis_value(int pos, int span_half)
{
    if (span_half <= 0) {
        return 0;
    }
    int v = ((pos - span_half) * 32767) / span_half;
    if (v > 32767) {
        v = 32767;
    } else if (v < -32767) {
        v = -32767;
    }
    return (int16_t)v;
}

/* Toggle operating mode and update the UI indicator. */
static void toggle_mode(void)
{
    tc_mode_t new_mode =
        (s_mode == MODE_VERTICAL) ? MODE_HORIZONTAL : MODE_VERTICAL;
    s_mode = new_mode;

    /* Short buzz for VERTICAL, slightly longer for HORIZONTAL. */
    vibration_trigger((new_mode == MODE_VERTICAL) ? VIB_VERTICAL_MS
                                                  : VIB_HORIZONTAL_MS,
                      0, 1);

    if (lvgl_port_lock(pdMS_TO_TICKS(100))) {
        ui_update_mode(new_mode);
        lvgl_port_unlock();
    }
    ESP_LOGI(TAG, "Mode: %s",
             (new_mode == MODE_VERTICAL) ? "VERTICAL" : "HORIZONTAL");
}

/* ---------------------------------------------------------------------
 * Touch gesture detection task
 *
 * State machine:
 *   IDLE  -> TOUCHING  on first touch sample
 *   TOUCHING -> IDLE   on touch release; gesture is classified then,
 *                      unless a long tap already fired during the hold.
 *
 * Each poll also samples the button GPIOs (buttons 0-15) and the mode
 * GPIO.  A press (high->low edge) on the mode GPIO toggles the output
 * mode (impulse <-> continuous); any change is forwarded to the receiver.
 *
 * Gesture rules:
 *   tap (finger never leaves the tap zone, |dx|,|dy| < TAP_MAX_MOVE_PX):
 *       held >= LONG_TAP_MS  -> mode toggle (fires immediately while the
 *                               finger is still down, then the touch is
 *                               consumed and ignored on release).  Once
 *                               the finger travels beyond TAP_MAX_MOVE_PX
 *                               the touch is a slide and can no longer
 *                               become a tap, even if it later holds still.
 *   |dy| >= SLIDE_MIN_PX (and |dy| > |dx|):
 *       impulse mode:
 *           dy < 0 (up)   -> negative impulse(s)
 *           dy > 0 (down) -> positive impulse(s)
 *           The number of impulses scales with the slide length, up to
 *           SLIDE_FULL_EVENTS for a full-screen slide.
 *       continuous mode:
 *           the active axis tracks the finger's distance from the middle
 *           of the screen while the slide lasts; button 9 stays on.
 *   The active edge line is highlighted for the duration of the slide.
 * --------------------------------------------------------------------- */
static void touch_task(void *arg)
{
    bool     was_touching = false;
    bool     long_fired   = false;
    bool     moved        = false;   /* finger left the tap zone (slide) */
    bool     cont_active  = false;   /* continuous slide in progress */
    uint16_t start_x = 0, start_y = 0;
    uint16_t last_x  = 0, last_y  = 0;
    int64_t  start_ms = 0;

    /* Impulse-mode slide segmentation: the current monotonic slide runs
     * from seg_start_y and has reached its furthest point seg_peak_y in
     * direction seg_dir (0 = none, -1 = up, +1 = down).  When the finger
     * reverses far enough the completed segment is emitted and a new one
     * begins, so the slide direction can change without lifting. */
    int seg_start_y = 0;
    int seg_peak_y  = 0;
    int seg_dir     = 0;

    uint16_t         prev_buttons = 0;
    /* Previous MODE_GPIO level for edge detection (active-low push button). */
    int prev_mode_level = gpio_get_level(MODE_GPIO);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(INPUT_POLL_MS));

        /* -- Sample button GPIOs (buttons 0-15, active low). -- */
        uint16_t btn_mask = 0;
        for (int i = 0; i < BTN_GPIO_COUNT; i++) {
            if (s_btn_gpios[i] >= 0 && gpio_get_level(s_btn_gpios[i]) == 0) {
                btn_mask |= (uint16_t)(1u << i);
            }
        }
        s_gpio_buttons = btn_mask;

        /* -- Sample the mode GPIO: each press (high->low edge) toggles
         *    between impulse and continuous output mode. -- */
        int mode_level = gpio_get_level(MODE_GPIO);
        if (mode_level == 0 && prev_mode_level != 0) {
            tc_output_mode_t out_mode =
                (s_output_mode == OUTPUT_CONTINUOUS) ? OUTPUT_IMPULSE
                                                     : OUTPUT_CONTINUOUS;
            s_output_mode = out_mode;
            cont_active   = false;
            s_axis_x = 0;
            s_axis_y = 0;
            /* Two long buzzes when switching to continuous mode, one long
             * buzz when switching to impulse mode. */
            if (out_mode == OUTPUT_CONTINUOUS) {
                vibration_trigger(VIB_LONG_MS, VIB_GAP_MS, 2);
            } else {
                vibration_trigger(VIB_LONG_MS, 0, 1);
            }
            ui_refresh_edges();
            gamepad_flush();
            ESP_LOGI(TAG, "Output mode: %s",
                     (out_mode == OUTPUT_CONTINUOUS) ? "CONTINUOUS"
                                                     : "IMPULSE");
        }
        prev_mode_level = mode_level;

        /* -- Forward button changes immediately. -- */
        if (btn_mask != prev_buttons) {
            prev_buttons = btn_mask;
            gamepad_flush();
        }

        if (!s_touch) {
            continue;
        }

        esp_lcd_touch_read_data(s_touch);

        esp_lcd_touch_point_data_t point = {0};
        uint8_t  num_pts = 0;
        esp_lcd_touch_get_data(s_touch, &point, &num_pts, 1);
        bool touched = (num_pts > 0);
        uint16_t x = point.x, y = point.y;

        if (touched) {
            last_x = x;
            last_y = y;
        }

        if (touched && !was_touching) {
            /* -- Touch start -- */
            start_x  = x;
            start_y  = y;
            start_ms = esp_timer_get_time() / 1000LL;
            was_touching = true;
            long_fired   = false;
            moved        = false;
            cont_active  = false;
            seg_start_y  = (int)y;
            seg_peak_y   = (int)y;
            seg_dir      = 0;
            ESP_LOGD(TAG, "touch start (%u,%u)", x, y);

        } else if (touched && was_touching && !long_fired) {
            /* -- Touch held -- fire long tap as soon as the threshold
             * elapses, without waiting for the finger to lift. -- */
            int dx   = (int)last_x - (int)start_x;
            int dy   = (int)last_y - (int)start_y;
            int adx  = dx < 0 ? -dx : dx;
            int ady  = dy < 0 ? -dy : dy;
            int dist = adx > ady ? adx : ady;
            int64_t held_ms = (esp_timer_get_time() / 1000LL) - start_ms;

            /* Once the finger travels beyond the tap zone the touch is a
             * slide; a later stationary hold must not be taken as a tap. */
            if (dist >= TAP_MAX_MOVE_PX) {
                moved = true;
            }

            if (!moved && held_ms >= (int64_t)LONG_TAP_MS) {
                toggle_mode();
                ui_refresh_edges();
                long_fired = true;

            } else if (s_output_mode == OUTPUT_CONTINUOUS) {
                /* Continuous mode: the finger always slides vertically;
                 * its distance from the middle of the screen drives the
                 * active axis.  The mode only selects which axis (Y in
                 * VERTICAL mode, X in HORIZONTAL mode) carries the value,
                 * so both modes react to the same up/down gesture. */
                int adisp     = ady;
                int pos       = last_y;
                int span_half = LCD_V_RES / 2;

                if (adisp >= SLIDE_MIN_PX) {
                    int16_t val = continuous_axis_value(pos, span_half);
                    if (s_mode == MODE_VERTICAL) {
                        s_axis_x = 0;
                        s_axis_y = val;
                    } else {
                        s_axis_x = val;
                        s_axis_y = 0;
                    }
                    gamepad_flush();
                    if (!cont_active) {
                        ui_set_active(true);
                        cont_active = true;
                    }
                } else if (cont_active) {
                    s_axis_x = 0;
                    s_axis_y = 0;
                    gamepad_flush();
                    ui_set_active(false);
                    cont_active = false;
                }

            } else if (moved) {
                /* Impulse mode: emit slides as the finger moves so the
                 * direction can be reversed without lifting.  The touch is
                 * split into monotonic vertical segments; a completed
                 * segment fires its impulses as soon as the finger turns
                 * back on itself by at least SLIDE_MIN_PX. */
                int py = (int)last_y;
                if (seg_dir == 0) {
                    /* Establish the initial slide direction, but only for a
                     * predominantly vertical move (horizontal swipes are
                     * ignored, as on release).  Switch the whole interface
                     * to the active colour as soon as the slide is
                     * detected, before any impulses are emitted. */
                    if (ady >= SLIDE_MIN_PX && ady >= adx) {
                        seg_dir    = (dy < 0) ? -1 : 1;
                        seg_peak_y = py;
                        ui_set_active(true);
                    }
                } else if (seg_dir > 0) {
                    if (py > seg_peak_y) {
                        seg_peak_y = py;
                    } else if (seg_peak_y - py >= SLIDE_MIN_PX) {
                        impulse_slide(seg_start_y, seg_peak_y);
                        seg_start_y = seg_peak_y;
                        seg_dir     = -1;
                        seg_peak_y  = py;
                    }
                } else { /* seg_dir < 0 */
                    if (py < seg_peak_y) {
                        seg_peak_y = py;
                    } else if (py - seg_peak_y >= SLIDE_MIN_PX) {
                        impulse_slide(seg_start_y, seg_peak_y);
                        seg_start_y = seg_peak_y;
                        seg_dir     = 1;
                        seg_peak_y  = py;
                    }
                }
            }

        } else if (!touched && was_touching) {
            /* -- Touch end -- classify gesture -- */
            was_touching = false;

            if (long_fired) {
                /* Mode already switched during the hold; nothing to do. */
                continue;
            }

            if (s_output_mode == OUTPUT_CONTINUOUS) {
                /* Release the proportional axis; leave button 9 on. */
                if (cont_active) {
                    ui_set_active(false);
                    cont_active = false;
                }
                s_axis_x = 0;
                s_axis_y = 0;
                gamepad_flush();
                continue;
            }

            int64_t dur_ms = (esp_timer_get_time() / 1000LL) - start_ms;

            ESP_LOGD(TAG, "touch end (%u,%u) dur=%lldms dir=%d",
                     last_x, last_y, dur_ms, seg_dir);

            /* Emit the final (still-open) slide segment.  Segments that
             * ended earlier through a direction reversal were already sent
             * while the finger was held.  Stationary taps and mostly
             * horizontal slides leave seg_dir at 0 and are ignored. */
            if (seg_dir != 0) {
                impulse_slide(seg_start_y, seg_peak_y);
                /* The interface was switched to the active colour when the
                 * slide was first detected; restore the idle colour now
                 * that the gesture has finished. */
                ui_set_active(false);
            }
        }
    }
}
#endif /* CONFIG_TC_HAS_TOUCH */

#if !CONFIG_TC_HAS_TOUCH
/* ---------------------------------------------------------------------
 * Button-only input task (boards without touch)
 *
 * Samples the pull-up button GPIOs (buttons 0-15, active low) and streams
 * a gamepad report whenever the button state changes.  The axes stay
 * centred; there is no display, touch or mode GPIO on these boards.
 * --------------------------------------------------------------------- */
static void button_task(void *arg)
{
    uint16_t prev_buttons = 0;

    /* Emit an initial report so the receiver knows the starting state. */
    gamepad_flush();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(INPUT_POLL_MS));

        uint16_t btn_mask = 0;
        for (int i = 0; i < BTN_GPIO_COUNT; i++) {
            if (s_btn_gpios[i] >= 0 && gpio_get_level(s_btn_gpios[i]) == 0) {
                btn_mask |= (uint16_t)(1u << i);
            }
        }
        s_gpio_buttons = btn_mask;

        if (btn_mask != prev_buttons) {
            prev_buttons = btn_mask;
            gamepad_flush();
        }
    }
}
#endif /* !CONFIG_TC_HAS_TOUCH */

/* ---------------------------------------------------------------------
 * app_main
 * --------------------------------------------------------------------- */
void app_main(void)
{
    ESP_LOGI(TAG, "ESP32 Touch Controller starting");

#if CONFIG_TC_HAS_DISPLAY
    hw_lcd_init();
#endif
#if CONFIG_TC_HAS_TOUCH
    hw_touch_init();
#endif
    hw_buttons_init();
#if CONFIG_TC_HAS_VIBRATION
    hw_vibration_init();
#endif

#if CONFIG_TC_HAS_DISPLAY
    lvgl_setup();

    /* Build UI inside the LVGL lock (-1 = wait indefinitely until LVGL task
     * gives up the mutex, which it does during its sleep phase).           */
    if (lvgl_port_lock(-1)) {
        ui_create();
        lvgl_port_unlock();
    }
#endif

    ESP_ERROR_CHECK(uart_gamepad_init());

#if CONFIG_TC_HAS_TOUCH
    xTaskCreate(touch_task, "touch", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "Running -- initial mode: VERTICAL");
#else
    xTaskCreate(button_task, "button", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "Running -- buttons only (no display/touch)");
#endif
}
