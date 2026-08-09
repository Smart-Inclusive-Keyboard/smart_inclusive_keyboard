# Hardware

## Supported boards

### Freenove FNK0104A (2.8" ILI9341, NonTouch) (default)

| Function           | GPIO        | Notes                              |
| ------------------ | ----------- | ---------------------------------- |
| LCD SPI CS         | 10          | ILI9341 4-wire SPI panel           |
| LCD SPI SCK        | 12          |                                    |
| LCD SPI MOSI (D0)  | 11          | data line written to the panel     |
| LCD SPI MISO (D1)  | 13          | not required (panel is write-only) |
| LCD DC             | 46          | data/command select                |
| LCD RST            | -1          | tied to module RST; software reset used |
| LCD BL             | 45          | active HIGH, LEDC PWM              |
| Gamepad 1 UART RX  | 2           | receive-only, 8-N-1, 115200 baud   |
| Gamepad 2 UART RX  | 3           | receive-only, 8-N-1, 115200 baud   |
| I2S MCLK / BCLK / LRCK / DOUT | 4 / 5 / 7 / 8 | on-board ES8311 codec + speaker |
| Codec I2C SDA / SCL | 16 / 15    | ES8311 codec, addr 0x18            |
| Codec PA enable    | 1           | class-D amplifier enable (active-low) |

- ESP32-S3 with PSRAM (`BOARD_HAS_PSRAM`).
- Native panel resolution is 240x320 portrait; the firmware
  presents it as 320x240 landscape. The ILI9341 honours the
  MADCTL row/column-exchange bit, so the landscape rotation is
  done in hardware (no software rotation in the flush path) --
  `board_t::display.swap_xy` stays `false`.
- No touchscreen (NonTouch variant); the firmware does not include
  touchscreen support.
- On-board I2S speaker via ES8311 codec. The board selects
  `BOARD_HAS_SPEAKER` so the narrator is compiled in by default;
  the I2S / codec pins above are hard-coded in
  `components/board/src/board_freenove_fnk0104a.c`.
- The two gamepads are wired to fixed receive-only UART RX pins,
  GPIO 2 (gamepad 1) and GPIO 3 (gamepad 2). These pins are
  hard-coded in the board file (the `CONFIG_SK_GAMEPAD*_UART_RX_GPIO`
  options are ignored for this board); the UART port and baud
  still come from menuconfig.
- If the panel is physically mounted upside-down in its enclosure,
  enable `CONFIG_SK_DISPLAY_ROTATE_180` (Display menu) to flip the
  image 180 degrees at the display-controller level.

### Generic ESP32-S3 / Generic ESP32

Placeholder boards. Edit
`components/board/src/board_generic_s3.c` (or
`board_generic_esp32.c`) to fill in the pin map for your custom
wiring, then select the matching board in menuconfig.

## Gamepad wiring

The external gamepads are separate boards that stream their HID
report into this firmware over a one-way (receive-only) UART
link. Two gamepads are supported; both drive the same on-screen
keyboard. Wire each gamepad's TX line to its configured RX GPIO
(`CONFIG_SK_GAMEPAD1_UART_RX_GPIO` / `CONFIG_SK_GAMEPAD2_UART_RX_GPIO`,
GPIO 2 / 3 by default) and share a common ground. The two
gamepads must use different UART ports. Each link is 8-N-1 at
`CONFIG_SK_GAMEPAD1_UART_BAUD` / `CONFIG_SK_GAMEPAD2_UART_BAUD`
baud (115200 by default); this firmware never transmits. Set a
gamepad's RX GPIO to -1 to disable it.

The report is a fixed 6-byte frame, identical to the HID report
emitted by the companion gamepad firmware
([clackups/esp32s3_dual_foc_gp](https://github.com/clackups/esp32s3_dual_foc_gp)):

```
byte 0:  buttons 0..7  (bit i set = GP_BTN_i pressed)
byte 1:  buttons 8..9  (bits 0..1) + 6 bits padding
byte 2:  X axis, signed 16-bit LE low byte
byte 3:  X axis high byte (-32767..32767, 0 = centred, positive = right)
byte 4:  Y axis, signed 16-bit LE low byte
byte 5:  Y axis high byte (-32767..32767, 0 = centred, positive = down)
```

The firmware refers to gamepad buttons by their zero-based bit
position (`GP_BTN_0`..`GP_BTN_9`) rather than by vendor letter
names (A/B/X/Y or Cross/Circle/Square/Triangle), so the same code
works across controllers whose silkscreens disagree. Bit `i` of
the buttons bitmap triggers `GP_BTN_i`. The `input_router` mapping
is:

- `GP_BTN_0` -> press selected key (or left mouse click in mouse mode)
- `GP_BTN_1` -> Shift + selected key (or right mouse click in mouse mode)
- `GP_BTN_2` -> Space
- `GP_BTN_3` -> Enter
- `GP_BTN_4` -> Backspace
- `GP_BTN_5` -> Ctrl + selected key (like GP_BTN_0 with Ctrl held)
- `GP_BTN_6` -> AltGr + selected key (like GP_BTN_0 with right Alt held)
- `GP_BTN_7` -> unused
- `GP_BTN_8` -> unused
- `GP_BTN_9` -> on down: keyboard mode; on up: mouse mode

Sticky modifiers (Shift / Ctrl / Alt / AltGr) stay engaged until
the next character key is pressed. The keyboard layout is changed
with the on-screen **Lng** key and the colour theme / enabled
languages from the **Mnu** settings menu (both on the function-key
row, right of F12).

### UART transport

The driver installs the UART in receive-only mode (no TX / RTS /
CTS pin is driven) and reads one 6-byte frame at a time. The
analog axes are reduced to discrete D-pad directions using
`CONFIG_SK_GAMEPAD_AXIS_DEADZONE`. To adapt the wire format, edit
`gamepad_parse_report()` in
`components/gamepad_uart/src/gamepad_uart.c`.

## Power

The Freenove FNK0104A is USB-powered (5 V). `CONFIG_BOARD_HAS_BATTERY`
is not yet selected for any shipping board -- when it is,
`board_t.battery_adc_channel` gates the on-screen battery indicator.
