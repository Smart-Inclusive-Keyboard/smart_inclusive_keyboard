/*
 * Freenove FNK0104A (2.8 inch ILI9341, NonTouch) board definition.
 *
 * Reference: https://github.com/Freenove/Freenove_ESP32_S3_Display
 * (FNK0104A / FNK0104AB variant). Pins cross-checked against the
 * vendor Arduino tutorials:
 *   - Display  : TFT_eSPI setup FNK0104AB_2.8_240x320_ILI9341.h
 *   - Audio/I2C: Tutorial_No_Touch Sketch_07.1_Music
 *
 * Panel:   ILI9341 4-wire SPI, native portrait 240 (W) x 320 (H).
 *          Exposed to the firmware as 320 x 240 landscape; the
 *          ILI9341 backend rotates in hardware via the MADCTL MV
 *          bit (no software rotation), so swap_xy stays false.
 *
 * Pins (Freenove reference firmware):
 *
 *   LCD SPI :  CS=10, SCK=12, MOSI(D0)=11, MISO(D1)=13, DC=46
 *   LCD RST :  -1 (tied to the ESP32-S3 module reset line; the
 *                  backend issues an ILI9341 software reset)
 *   LCD BL  :  45 (active HIGH, LEDC PWM)
 *
 *   On-board ES8311 audio codec + amplifier + speaker:
 *       MCLK = 4, BCLK = 5, LRCK (WS) = 7, DOUT = 8
 *       (ESP32-S3 -> codec serial data; the codec's mic DIN on
 *        GPIO 6 is unused by the narrator output path).
 *       Codec I2C: SDA = 16, SCL = 15, 7-bit address 0x18.
 *       The class-D amplifier enable ("AP_ENABLE") is on GPIO 1.
 *
 *   External gamepad UARTs: two companion gamepad boards stream
 *       their HID reports TX-only into this board's RX pins
 *       (8-N-1, baud from Kconfig). Per the board bring-up the RX
 *       pins are fixed at GPIO 2 (gamepad 1) and GPIO 3
 *       (gamepad 2) -- these are broken out on the FNK0104A and
 *       are otherwise only used for the (unpopulated here) SD-card
 *       data lines. The interface is receive-only; no TX / RTS /
 *       CTS line is driven.
 */

#include "board.h"

#if CONFIG_SK_BOARD_FREENOVE_FNK0104A

/* Fixed gamepad RX pins for this board (see header comment). */
#define SK_FNK0104A_GAMEPAD1_RX 2
#define SK_FNK0104A_GAMEPAD2_RX 3

const board_t g_board = {
    .name = "Freenove FNK0104A (2.8\" ILI9341)",
    .display_type = BOARD_DISPLAY_ILI9341,
    .display = {
        .cs  = 10,
        .sck = 12,
        .d0  = 11,    /* MOSI (SDA) */
        .d1  = 13,    /* MISO */
        .d2  = -1,
        .d3  = -1,
        .dc  = 46,
        .rst = -1,    /* tied to module RST; SW reset used instead */
        .te  = -1,
        .bl  = 45,
        .bl_active_low = false,
        /* Native panel is portrait 240 x 320; the ILI9341 backend
         * rotates to landscape in hardware (MADCTL MV), so the
         * firmware sees a 320 x 240 framebuffer and swap_xy stays
         * false. */
        .width  = 320,
        .height = 240,
        .swap_xy = false,
    },
    /* External gamepad UART links. RX pins are fixed at GPIO 2 / 3
     * on this board; only the port and baud come from Kconfig. */
    .gamepad_uart = {
        { .port = CONFIG_SK_GAMEPAD1_UART_PORT,
          .rx   = SK_FNK0104A_GAMEPAD1_RX,
          .baud = CONFIG_SK_GAMEPAD1_UART_BAUD },
        { .port = CONFIG_SK_GAMEPAD2_UART_PORT,
          .rx   = SK_FNK0104A_GAMEPAD2_RX,
          .baud = CONFIG_SK_GAMEPAD2_UART_BAUD },
    },
    .i2s = {
        /* On-board ES8311 codec wiring -- fixed by the PCB. */
        .mclk = 4,
        .bclk = 5,
        .lrck = 7,
        .dout = 8,
        .port = 0,
    },
    .codec = {
        /* ES8311 at 7-bit address 0x18 on its own I2C bus
         * (SDA = 16, SCL = 15). The class-D amplifier enable is
         * a discrete MCU pin (GPIO 1) and is active-low on this
         * board: the Freenove reference firmware holds AP_ENABLE
         * LOW while audio plays, so the codec must drive it LOW to
         * un-mute the speaker. */
        .i2c_port = 0,
        .sda      = 16,
        .scl      = 15,
        .addr     = 0x18,
        .freq_hz  = 400000,
        .pa_pin   = 1,
        .pa_active_low = true,
    },
    .battery_adc_channel = -1,
};

/* Compile-time guards against pin overlap between the gamepad
 * UART RX pins and the display SPI bus / audio I2S bus / codec
 * I2C bus. The pin sets owned by each subsystem on this board:
 *   display : { 10, 11, 12, 13, 45, 46 }
 *   audio   : { 4, 5, 6, 7, 8 } (I2S) + { 1 } (PA enable)
 *   codec   : { 15, 16 } (I2C)
 * The fixed gamepad RX pins (2, 3) fall outside all of these. */
#include <assert.h>
#define SK_FNK_PIN_CONFLICTS_DISPLAY(p) ( \
    (p) == 10 || (p) == 11 || (p) == 12 || (p) == 13 || \
    (p) == 45 || (p) == 46)
#define SK_FNK_PIN_CONFLICTS_AUDIO(p) ( \
    (p) == 1  || (p) == 4  || (p) == 5  || (p) == 6  || \
    (p) == 7  || (p) == 8  || (p) == 15 || (p) == 16)
#define SK_FNK_GAMEPAD_PIN_OK(p) \
    (!SK_FNK_PIN_CONFLICTS_DISPLAY(p) && \
     !SK_FNK_PIN_CONFLICTS_AUDIO(p))

_Static_assert(SK_FNK_GAMEPAD_PIN_OK(SK_FNK0104A_GAMEPAD1_RX),
    "Freenove FNK0104A: gamepad 1 UART RX must not overlap display/audio pins");

_Static_assert(SK_FNK_GAMEPAD_PIN_OK(SK_FNK0104A_GAMEPAD2_RX),
    "Freenove FNK0104A: gamepad 2 UART RX must not overlap display/audio pins");

_Static_assert(CONFIG_SK_GAMEPAD1_UART_PORT != CONFIG_SK_GAMEPAD2_UART_PORT,
    "Freenove FNK0104A: gamepad 1 and 2 must use different UART ports");

#endif /* CONFIG_SK_BOARD_FREENOVE_FNK0104A */
