# Input controller for the Smart Inclusive Keyboard

Firmware for the **Waveshare ESP32-C6-Touch-LCD-1.47** development board
that turns the capacitive touch screen into a simple gamepad-style
navigation controller. Touch gestures and dedicated GPIO buttons are
interpreted on-device and streamed as 6-byte HID gamepad reports over a
transmit-only UART to the receiver in the main unit.

A second, display-less board type is also supported for setups that only
need physical buttons (see [Board types](#board-types)).

## Overview

The board polls its AXS5106L capacitive touch controller, classifies the
touch as a long tap or a slide, samples a set of pull-up button GPIOs,
and emits the corresponding gamepad event on UART1. The 1.47" JD9853 LCD
displays a schematic navigation guide and a bright edge line that
indicates the current axis mode.

## Board types

The target board is selected under **Touch Controller -> Hardware board**
in `menuconfig`:

| Board | Display | Touch | Inputs |
|-------|---------|-------|--------|
| Waveshare ESP32-C6-Touch-LCD-1.47 | Yes | Yes | Touch gestures, buttons 0-15, mode GPIO, vibration motor |
| Generic ESP32 (no display) | No | No | Buttons 0-15 only |

The **Generic ESP32** board (`CONFIG_TC_BOARD_GENERIC_ESP32`) leaves out
the LCD, capacitive touch, gesture detection and mode GPIO entirely. It
only samples the pull-up button inputs and streams them as game
controller buttons over the transmit-only UART, so it runs on any plain
ESP32-family chip without a display.

## Hardware

Waveshare ESP32-C6-Touch-LCD-1.47 (172x320 portrait IPS):

| Function | Bus | Pins |
|----------|-----|------|
| LCD (JD9853) | SPI2 | SCLK=1, MOSI=2, MISO=3, CS=14, DC=15, RST=22, BL=23 |
| Touch (AXS5106L) | I2C0 | SDA=18, SCL=19, INT=21, RST=20 (addr 0x63) |
| Gamepad output | UART1 | TX=7, 115200 8-N-1, transmit-only |
| Vibration motor | GPIO | GPIO4, active high (optional, `CONFIG_TC_HAS_VIBRATION`) |

## Button and mode inputs

Up to sixteen pull-up button inputs (buttons 0-15) plus a mode input (on
touch boards) are configured (defaults below, all configurable in
`menuconfig`). All buttons are unassigned by default; set a button GPIO
to a valid pin to enable it, or to `-1` to leave it unassigned:

| Input | Default GPIO | Action |
|-------|--------------|--------|
| Button 0..15 | -1 (unassigned) | Game controller button 0..15 while pulled low |
| Mode | 6 | Select axis output mode (touch boards only) |

Each button GPIO is active low: pulling it to ground reports the
corresponding game controller button as pressed.  A button set to `-1`
is skipped and never reports as pressed.

The **mode** GPIO is an active-low push button that toggles the axis
output mode: each press switches between **impulse** and **continuous**
mode. The button does not need to be held; the controller starts in
impulse mode. The mode GPIO exists only on boards with touch; the
generic board has no mode input.

> Note: all buttons default to unassigned (-1) and the mode GPIO to
> GPIO6; assign button GPIOs and remap the mode GPIO in `menuconfig` to
> free pins as required.

## Gesture mapping

| Gesture | Action |
|---------|--------|
| Slide up | Negative axis output |
| Slide down | Positive axis output |
| Long tap (>= 500 ms) | Toggle VERTICAL / HORIZONTAL mode |

Short taps are not used; on-screen gamepad buttons come exclusively from
the button GPIOs.

In **VERTICAL** mode the axis output is sent on the Y axis; in
**HORIZONTAL** mode it is sent on the X axis.

### Impulse mode (default)

A slide that spans the full length of the screen produces several axis
events; shorter slides produce proportionally fewer events (always at
least one). The number of events for a full-length slide is configurable
via `CONFIG_TC_SLIDE_FULL_EVENTS` (default 3).

The slide is tracked while the finger is held, so its direction can be
reversed without lifting: sliding up and then back down (or vice versa)
emits impulses in each direction in turn, no release required.

### Continuous mode

When continuous mode is selected, button 9 is held permanently on and
the active axis is set proportional to the sliding finger's distance
from the middle of the touch screen (fully deflected at the edges,
centred at the middle). Releasing the finger returns the axis to centre
while button 9 stays on. The long tap still switches between horizontal
and vertical movement, exactly as in impulse mode.

The finger always slides vertically (up/down); the mode only selects
which axis carries the value, so both VERTICAL and HORIZONTAL modes
respond to the same gesture.

A long tap toggles the mode as soon as `CONFIG_TC_LONG_TAP_MS` elapses
while the finger is still held down -- it does not wait for the finger to
be lifted. Once the toggle has fired, the rest of that touch (including
the eventual release) is ignored. A long tap is only recognized when the
finger stays within `CONFIG_TC_TAP_MAX_MOVE_PX` of the initial touch; if
the finger slides beyond that distance the touch is treated as a slide
and holding it still afterwards will not trigger a tap.

## Mode indicator

The screen shows two arrows and a short centre divider line (about the
width of the arrows). The current mode is shown by a bright line along
one edge of the screen:

- **VERTICAL mode** -- line along the **right** edge.
- **HORIZONTAL mode** -- line along the **top** edge.

All on-screen elements (the arrows, the centre divider and the edge
lines) share a single colour and switch together between an idle and an
active colour. In impulse mode they use `CONFIG_TC_COLOR_IMPULSE_IDLE`
while idle and switch to `CONFIG_TC_COLOR_IMPULSE_ACTIVE` as soon as a
sliding gesture is detected. In continuous mode they use the direction
indicator colour pair: `CONFIG_TC_COLOR_CONTINUOUS_IDLE` (yellow) while
idling and `CONFIG_TC_COLOR_CONTINUOUS_ACTIVE` (orange) while a sliding
gesture is detected.

## Vibration feedback

Boards with a vibration motor (`CONFIG_TC_HAS_VIBRATION`, enabled by
default on the Waveshare ESP32-C6-Touch-LCD-1.47 and driven on GPIO6 by
default) give haptic feedback for the main events:

| Event | Vibration |
|-------|-----------|
| Axis impulse HID report issued | Very short buzz (`TC_VIB_IMPULSE_MS`) |
| Switch to VERTICAL mode | Short buzz (`TC_VIB_VERTICAL_MS`) |
| Switch to HORIZONTAL mode | Slightly longer buzz (`TC_VIB_HORIZONTAL_MS`) |
| Switch to continuous mode | Two long buzzes (`TC_VIB_LONG_MS`, gap `TC_VIB_GAP_MS`) |
| Switch to impulse mode | One long buzz (`TC_VIB_LONG_MS`) |

The motor is driven active high. All pulse durations and the vibration
motor GPIO are configurable in `menuconfig` under **Touch Controller ->
Vibration feedback**. Disable `TC_HAS_VIBRATION` to compile the feature
out entirely.

## UART gamepad report

Every event sends a 6-byte report, as defined in the main unit:

```
byte 0    : buttons 0-7   (bit n = button n pressed)
byte 1    : buttons 8-15  (bit n = button n+8 pressed)
bytes 2-3 : X axis, signed 16-bit little-endian (0 = centre)
bytes 4-5 : Y axis, signed 16-bit little-endian (0 = centre)
```

All 16 button bits are usable: buttons 0-7 occupy byte 0 and buttons
8-15 occupy byte 1.

## Configuration

Tunable options are exposed under **Touch Controller** in `menuconfig`:

| Option | Default | Description |
|--------|---------|-------------|
| `TC_HAS_VIBRATION` | y (Waveshare) | Vibration motor present |
| `TC_SCREEN_BRIGHTNESS` | 50 | Backlight brightness (percent) |
| `TC_COLOR_BACKGROUND` | 0x000000 | Background colour (0xRRGGBB) |
| `TC_COLOR_IMPULSE_IDLE` | 0x3333cc | Impulse-mode idle colour (0xRRGGBB) |
| `TC_COLOR_IMPULSE_ACTIVE` | 0x33ccff | Impulse-mode active colour (0xRRGGBB) |
| `TC_COLOR_CONTINUOUS_IDLE` | 0xffff00 | Continuous-mode idle indicator (yellow) |
| `TC_COLOR_CONTINUOUS_ACTIVE` | 0xff8000 | Continuous-mode active indicator (orange) |
| `TC_UART_TX_GPIO` | 7 | UART TX GPIO number |
| `TC_UART_BAUD` | 115200 | UART baud rate |
| `TC_BTN0_GPIO` .. `TC_BTN15_GPIO` | -1 (all unassigned) | Button 0-15 input GPIO numbers (-1 = unassigned) |
| `TC_MODE_GPIO` | 5 | Impulse/continuous mode input GPIO (touch boards) |
| `TC_SLIDE_MIN_PX` | 25 | Minimum travel to classify a slide |
| `TC_TAP_MAX_MOVE_PX` | 15 | Maximum movement for a long tap |
| `TC_LONG_TAP_MS` | 500 | Long-tap threshold (mode toggle) |
| `TC_SLIDE_FULL_EVENTS` | 3 | Axis events for a full-length slide (impulse mode) |
| `TC_VIBRATION_GPIO` | 6 | Vibration motor GPIO (active high) |
| `TC_VIB_IMPULSE_MS` | 15 | Very short buzz on each axis impulse report |
| `TC_VIB_VERTICAL_MS` | 40 | Short buzz on switching to VERTICAL mode |
| `TC_VIB_HORIZONTAL_MS` | 80 | Slightly longer buzz on switching to HORIZONTAL mode |
| `TC_VIB_LONG_MS` | 150 | Long buzz (continuous x2, impulse mode x1) |
| `TC_VIB_GAP_MS` | 80 | Gap between the two continuous-mode buzzes |

## Building and flashing

Requires ESP-IDF 6.0 or newer.

```sh
idf.py set-target esp32c6
idf.py menuconfig      # optional: adjust Touch Controller settings
idf.py build
idf.py -p <PORT> flash monitor
```

Dependencies (`espressif/esp_lcd_touch`, `espressif/esp_lvgl_port`,
`lvgl/lvgl`) are resolved automatically by the IDF component manager.

### Build presets

`CMakePresets.json` defines two configure presets that pair a build
directory with a defaults file:

- `default` -- primary Waveshare ESP32-C6 controller
  (`sdkconfig.defaults`).
- `secondary_esp32c3` -- display-less ESP32-C3 button controller
  (`sdkconfig.defaults.secondary_esp32c3`, generic board with buttons on
  UART TX GPIO10 and buttons 0-6 on GPIO0-7).

Select a preset with, for example, `idf.py -B build/secondary_esp32s3
@CMakePresets.json` or your IDE's CMake preset picker.

## Repository layout

```
main/                  application code (main.c, uart_gamepad.*)
main/Kconfig.projbuild  menuconfig options
components/             board LCD and touch drivers
sdkconfig.defaults      default build configuration (primary C6 board)
sdkconfig.defaults.secondary_esp32s3  defaults for the display-less S3 board
CMakePresets.json       build presets pairing build dirs with defaults
```

