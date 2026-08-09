/*
 * ILI9341 SPI display backend.
 *
 * Drives a 240x320 ILI9341 TFT over a plain 4-wire SPI bus using
 * the ESP-IDF esp_lcd panel-IO layer (esp_lcd_new_panel_io_spi),
 * which manages the CS / DC toggling and the DMA transfers for us.
 * The ILI9341 honours the MADCTL "MV" (row/column exchange) bit,
 * so the landscape orientation is done in hardware and no software
 * rotation is needed at flush time.
 *
 * Board wiring is taken from board_get()->display:
 *   cs  -> chip select
 *   sck -> SCLK
 *   d0  -> MOSI (SDA)
 *   d1  -> MISO (-1 when the panel is write-only)
 *   dc  -> data/command select
 *   rst -> hardware reset (-1 when tied to the board's RST line;
 *          a software reset (SWRESET) is issued in that case)
 *   bl  -> backlight enable (LEDC PWM, active level per
 *          bl_active_low)
 *
 * The logical framebuffer is RGB565 in PSRAM, exposed to the UI
 * layer in landscape (width x height as configured by the board,
 * e.g. 320x240). Pixels are byte-swapped into a small internal
 * DMA-capable row scratch buffer before each transfer because the
 * ILI9341 expects RGB565 big-endian on the wire while the
 * framebuffer holds native little-endian uint16_t.
 */

#include "sdkconfig.h"

#if CONFIG_BOARD_HAS_DISPLAY_ILI9341

#include <stdlib.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#include <esp_err.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <driver/gpio.h>
#include <driver/ledc.h>
#include <driver/spi_master.h>
#include <esp_lcd_panel_io.h>

#include "board.h"
#include "display.h"

static const char *TAG = "display_ili9341";

/* Framebuffer integration hook implemented in display.c. */
extern void display_register_backend(uint16_t *fb, int w, int h,
                                     void (*flush)(int, int, int, int),
                                     void (*set_backlight)(int));

/* ---------------- SPI constants ---------------- */

#define ILI_SPI_HOST        SPI2_HOST
#define ILI_SPI_CLOCK_HZ    (40 * 1000 * 1000)

/* ILI9341 command set (subset used here). */
#define ILI_CMD_SWRESET     0x01
#define ILI_CMD_SLPOUT      0x11
#define ILI_CMD_INVON       0x21
#define ILI_CMD_DISPON      0x29
#define ILI_CMD_CASET       0x2A
#define ILI_CMD_PASET       0x2B
#define ILI_CMD_RAMWR       0x2C
#define ILI_CMD_MADCTL      0x36
#define ILI_CMD_COLMOD      0x3A

/* MADCTL bits. */
#define ILI_MADCTL_MY       0x80
#define ILI_MADCTL_MX       0x40
#define ILI_MADCTL_MV       0x20
#define ILI_MADCTL_ML       0x10
#define ILI_MADCTL_BGR      0x08

/* Landscape (MV set) with BGR panel colour order. Matches the
 * Freenove reference (TFT_eSPI rotation 1, TFT_RGB_ORDER = BGR).
 * This yields a 320 (W) x 240 (H) logical framebuffer from the
 * native 240x320 portrait panel. When CONFIG_SK_DISPLAY_ROTATE_180
 * is set, MX and MY are also set, which flips both axes and
 * presents the same 320x240 landscape framebuffer rotated 180
 * degrees (for boards mounted upside-down in their enclosure). */
#if CONFIG_SK_DISPLAY_ROTATE_180
#define ILI_MADCTL_LANDSCAPE (ILI_MADCTL_MV | ILI_MADCTL_BGR | \
                              ILI_MADCTL_MX | ILI_MADCTL_MY)
#else
#define ILI_MADCTL_LANDSCAPE (ILI_MADCTL_MV | ILI_MADCTL_BGR)
#endif

/* ---------------- Backlight (LEDC PWM) ---------------- */

#define BL_LEDC_TIMER       LEDC_TIMER_3
#define BL_LEDC_MODE        LEDC_LOW_SPEED_MODE
#define BL_LEDC_CHANNEL     LEDC_CHANNEL_1
#define BL_LEDC_DUTY_RES    LEDC_TIMER_8_BIT
#define BL_LEDC_DUTY_MAX    ((1 << 8) - 1)
#define BL_LEDC_FREQ_HZ     50000

/* ---------------- State ---------------- */

static esp_lcd_panel_io_handle_t s_io;
static uint16_t *s_fb;              /* logical landscape, PSRAM     */
static uint16_t *s_row_buf;         /* internal DMA scratch, 1 row  */
static SemaphoreHandle_t s_row_done;/* signalled per row transfer   */
static int   s_w, s_h;
static int   s_bl_pin;
static bool  s_bl_active_lo;
static int   s_rst_pin;

/* Colour-transfer completion callback (runs in ISR context). The
 * esp_lcd SPI backend queues each colour transfer asynchronously
 * and returns before the DMA finishes, so we gate the row-scratch
 * reuse on this signal to avoid overwriting a buffer that is still
 * being clocked out. */
static bool color_trans_done(esp_lcd_panel_io_handle_t io,
                             esp_lcd_panel_io_event_data_t *edata,
                             void *user_ctx)
{
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(s_row_done, &hp);
    return hp == pdTRUE;
}

/* ---------------- Command helpers ---------------- */

static void ili_cmd(uint8_t cmd, const uint8_t *data, size_t len)
{
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_io, cmd, data, len));
}

static void hw_reset(void)
{
    if (s_rst_pin >= 0) {
        gpio_config_t g = {
            .intr_type    = GPIO_INTR_DISABLE,
            .mode         = GPIO_MODE_OUTPUT,
            .pin_bit_mask = (1ULL << s_rst_pin),
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&g));
        gpio_set_level((gpio_num_t)s_rst_pin, 1);
        vTaskDelay(pdMS_TO_TICKS(5));
        gpio_set_level((gpio_num_t)s_rst_pin, 0);
        vTaskDelay(pdMS_TO_TICKS(20));
        gpio_set_level((gpio_num_t)s_rst_pin, 1);
        vTaskDelay(pdMS_TO_TICKS(120));
    } else {
        /* RST tied to the board reset line: fall back to the
         * controller's software reset. */
        ili_cmd(ILI_CMD_SWRESET, NULL, 0);
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}

static void ili9341_init_sequence(void)
{
    /* Exit sleep. The panel needs ~120 ms after SLPOUT before it
     * accepts further commands reliably. */
    ili_cmd(ILI_CMD_SLPOUT, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(120));

    /* 16 bits/pixel (RGB565). */
    ili_cmd(ILI_CMD_COLMOD, (const uint8_t[]){ 0x55 }, 1);

    /* Orientation + colour order. */
    ili_cmd(ILI_CMD_MADCTL, (const uint8_t[]){ ILI_MADCTL_LANDSCAPE }, 1);

    /* The Freenove panel is wired for display inversion ON
     * (TFT_INVERSION_ON in the reference setup). */
    ili_cmd(ILI_CMD_INVON, NULL, 0);

    /* Turn the panel on. */
    ili_cmd(ILI_CMD_DISPON, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
}

/* ---------------- Backlight ---------------- */

static void backlight_init(void)
{
    if (s_bl_pin < 0) return;

    gpio_config_t g = {
        .intr_type    = GPIO_INTR_DISABLE,
        .mode         = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << s_bl_pin),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&g));
    gpio_set_level((gpio_num_t)s_bl_pin, s_bl_active_lo ? 0 : 1);

    ledc_timer_config_t tcfg = {
        .speed_mode      = BL_LEDC_MODE,
        .duty_resolution = BL_LEDC_DUTY_RES,
        .timer_num       = BL_LEDC_TIMER,
        .freq_hz         = BL_LEDC_FREQ_HZ,
        .clk_cfg         = LEDC_USE_RC_FAST_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&tcfg));

    uint32_t duty = s_bl_active_lo ? 0 : BL_LEDC_DUTY_MAX;

    ledc_channel_config_t ccfg = {
        .speed_mode = BL_LEDC_MODE,
        .channel    = BL_LEDC_CHANNEL,
        .timer_sel  = BL_LEDC_TIMER,
        .intr_type  = LEDC_INTR_DISABLE,
        .gpio_num   = s_bl_pin,
        .duty       = duty,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ccfg));
}

static void backlight_set(int percent)
{
    if (s_bl_pin < 0) return;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    uint32_t on_duty = (percent * BL_LEDC_DUTY_MAX) / 100;
    uint32_t duty    = s_bl_active_lo ? (BL_LEDC_DUTY_MAX - on_duty) : on_duty;

    ledc_set_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL, duty);
    ledc_update_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL);
}

/* ---------------- Flush ---------------- */

/* Program the ILI9341 address window (inclusive column / page
 * ranges) that the following RAMWR burst will fill. */
static void set_window(int x0, int y0, int x1, int y1)
{
    uint8_t caset[4] = {
        (uint8_t)(x0 >> 8), (uint8_t)(x0 & 0xFF),
        (uint8_t)(x1 >> 8), (uint8_t)(x1 & 0xFF),
    };
    uint8_t paset[4] = {
        (uint8_t)(y0 >> 8), (uint8_t)(y0 & 0xFF),
        (uint8_t)(y1 >> 8), (uint8_t)(y1 & 0xFF),
    };
    ili_cmd(ILI_CMD_CASET, caset, sizeof(caset));
    ili_cmd(ILI_CMD_PASET, paset, sizeof(paset));
}

static void flush_bbox(int x0, int y0, int x1, int y1)
{
    if (!s_io || !s_fb || !s_row_buf) return;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= s_w) x1 = s_w - 1;
    if (y1 >= s_h) y1 = s_h - 1;
    if (x1 < x0 || y1 < y0) return;

    int bw = x1 - x0 + 1;
    set_window(x0, y0, x1, y1);

    /* Stream the dirty rectangle one row at a time. The first row
     * uses RAMWR (0x2C) to anchor the write pointer at the CASET /
     * PASET origin; subsequent rows continue the memory write with
     * no command byte (cmd = -1), relying on the ILI9341's implicit
     * GRAM auto-increment (toggling CS between rows does not reset
     * the pointer on this controller).
     *
     * esp_lcd queues each colour transfer asynchronously, so we
     * wait on s_row_done before refilling s_row_buf for the next
     * row -- otherwise the DMA could still be reading the scratch
     * we are about to overwrite. */
    for (int y = y0; y <= y1; ++y) {
        const uint16_t *src = s_fb + (size_t)y * s_w + x0;
        for (int i = 0; i < bw; ++i) {
            uint16_t p = src[i];
            /* RGB565 byte-swap: framebuffer little-endian -> panel
             * big-endian */
            s_row_buf[i] = (uint16_t)((p << 8) | (p >> 8));
        }
        int cmd = (y == y0) ? ILI_CMD_RAMWR : -1;
        ESP_ERROR_CHECK(esp_lcd_panel_io_tx_color(
            s_io, cmd, s_row_buf, (size_t)bw * 2));
        xSemaphoreTake(s_row_done, portMAX_DELAY);
    }
}

/* ---------------- Init ---------------- */

void display_ili9341_init(void)
{
    const board_t *b = board_get();
    s_w            = b->display.width;
    s_h            = b->display.height;
    s_bl_pin       = b->display.bl;
    s_bl_active_lo = b->display.bl_active_low;
    s_rst_pin      = b->display.rst;

    /* 1. SPI bus: single MOSI data line + SCLK (+ optional MISO). */
    int max_row_pixels = (s_w > s_h) ? s_w : s_h;
    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = b->display.d0,
        .miso_io_num     = b->display.d1,
        .sclk_io_num     = b->display.sck,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = max_row_pixels * 2 + 16,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(ILI_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num       = b->display.cs,
        .dc_gpio_num       = b->display.dc,
        .spi_mode          = 0,
        .pclk_hz           = ILI_SPI_CLOCK_HZ,
        .trans_queue_depth = 10,
        .lcd_cmd_bits      = 8,
        .lcd_param_bits    = 8,
        .on_color_trans_done = color_trans_done,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)ILI_SPI_HOST, &io_cfg, &s_io));

    s_row_done = xSemaphoreCreateBinary();
    if (!s_row_done) {
        ESP_LOGE(TAG, "Failed to create row-completion semaphore");
        abort();
    }

    /* 2. Framebuffer (logical landscape, in PSRAM) and per-row DMA
     * scratch (internal DMA-capable RAM). */
    size_t fb_bytes = (size_t)s_w * s_h * sizeof(uint16_t);
    s_fb = heap_caps_malloc(fb_bytes, MALLOC_CAP_SPIRAM);
    s_row_buf = heap_caps_malloc((size_t)max_row_pixels * 2,
                                 MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!s_fb || !s_row_buf) {
        ESP_LOGE(TAG, "Failed to allocate framebuffer (%u B PSRAM) or "
                      "row scratch (%u B internal DMA)",
                 (unsigned)fb_bytes, (unsigned)(max_row_pixels * 2));
        abort();
    }
    memset(s_fb, 0, fb_bytes);

    /* 3. Reset + vendor init. */
    hw_reset();
    ili9341_init_sequence();

    /* 4. Paint the (black) framebuffer before raising the backlight
     * so the panel does not flash power-on GRAM garbage. */
    flush_bbox(0, 0, s_w - 1, s_h - 1);

    /* 5. Backlight on. */
    backlight_init();

    /* 6. Hand the framebuffer to the front-end. */
    display_register_backend(s_fb, s_w, s_h, flush_bbox, backlight_set);

    ESP_LOGI(TAG, "ILI9341 up: %dx%d, fb=%u B in PSRAM",
             s_w, s_h, (unsigned)fb_bytes);
}

#endif /* CONFIG_BOARD_HAS_DISPLAY_ILI9341 */
