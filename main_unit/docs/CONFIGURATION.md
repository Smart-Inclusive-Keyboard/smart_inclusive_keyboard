# Configuration

All build-time options live under
**SMART KEYBOARD** in `idf.py menuconfig`.

## Board selection

`SK_BOARD_*` -- exactly one. The default is
`SK_BOARD_FREENOVE_FNK0104A` (Freenove 2.8" ILI9341, NonTouch).
Selecting a board automatically
"selects" the capability symbols that gate optional components:

| Symbol                       | Meaning                                |
| ---------------------------- | -------------------------------------- |
| `BOARD_HAS_PSRAM`            | Component code may allocate from PSRAM |
| `BOARD_HAS_DISPLAY`          | Some panel driver is configured        |
| `BOARD_HAS_DISPLAY_ILI9341`  | Build the ILI9341 SPI backend          |
| `BOARD_HAS_SPEAKER`          | I2S DAC wired -> compile in audio +    |
|                              | narrator                               |
| `BOARD_HAS_BATTERY`          | VBAT exposed -> battery indicator      |

The Audio / Narrator menu is **hidden** unless `BOARD_HAS_PSRAM`
and `BOARD_HAS_SPEAKER` are both set, because narration requires
both to be useful.

## Display

`SK_DISPLAY_BACKLIGHT_PERCENT` (0..100) sets the panel backlight
brightness applied at boot, as a percentage of full brightness.
Boards with a controllable (LEDC PWM) backlight honour it; boards
without one ignore it. The default is tuned per board: 50% for the
Freenove FNK0104A.

`SK_DISPLAY_ROTATE_180` flips the panel image 180 degrees (both
axes mirrored) at the display-controller level. Useful when the
panel is physically mounted upside-down in its enclosure, e.g. the
Freenove FNK0104A.

## Gamepad transport

The external gamepads are separate boards that stream their HID
report into this firmware over a one-way (receive-only) UART
link (8-N-1). This firmware never transmits. Two gamepads are
supported; both feed the same `gamepad_event_t` queue and drive
the same on-screen keyboard. The 6-byte report format and queue
feed a transport-agnostic `input_router`.

| Option                            | Default | Range                |
| --------------------------------- | ------- | -------------------- |
| `SK_GAMEPAD_AXIS_DEADZONE`        | 8000    | 0..32767             |
| `SK_GAMEPAD1_UART_PORT`           | 1       | 1..2 (UART1 / UART2) |
| `SK_GAMEPAD1_UART_RX_GPIO`        | 21      | any GPIO, -1 = off   |
| `SK_GAMEPAD1_UART_BAUD`           | 115200  | any baud rate        |
| `SK_GAMEPAD2_UART_PORT`           | 2       | 1..2 (UART1 / UART2) |
| `SK_GAMEPAD2_UART_RX_GPIO`        | 38      | any GPIO, -1 = off   |
| `SK_GAMEPAD2_UART_BAUD`           | 115200  | any baud rate        |

Each RX pin is connected to a gamepad's TX line; no TX / RTS /
CTS pin is driven. The two gamepads must use different UART ports.
Set a gamepad's `*_UART_RX_GPIO` to -1 to disable it.

> **Note:** On the Freenove FNK0104A board the gamepad RX pins are
> hard-coded to GPIO 2 (gamepad 1) and GPIO 3 (gamepad 2) in the
> board file, so the `SK_GAMEPAD*_UART_RX_GPIO` options above have
> no effect for that board. The UART port and baud options still
> apply.
`SK_GAMEPAD_AXIS_DEADZONE` is shared by both gamepads and applied
to the signed 16-bit analog axes carried in the report. See
[HARDWARE.md](HARDWARE.md) for the wire format and button map.

## HID transport

The firmware acts as a HID peripheral over either Bluetooth LE or
USB. The transport is selected at build time via
`SK_HID_TRANSPORT`; each option is only offered when the target
SoC actually has the corresponding controller
(`SOC_BLE_SUPPORTED` / `SOC_USB_OTG_SUPPORTED`).

| Option                  | Targets        | Backend                  |
| ----------------------- | -------------- | ------------------------ |
| `SK_HID_TRANSPORT_BLE`  | classic ESP32, S3 | NimBLE GATT HID       |
| `SK_HID_TRANSPORT_USB`  | ESP32-S2, S3, P4  | TinyUSB composite HID |

The classic ESP32 only exposes the BLE option; ESP32-S2 only the
USB option; ESP32-S3 lets you pick either.

## BLE HID

Only visible when `SK_HID_TRANSPORT_BLE` is selected.

| Option                  | Default        | Purpose                  |
| ----------------------- | -------------- | ------------------------ |
| `SK_BLE_DEVICE_NAME`    | `SmartKeyboard`| advertised local name    |
| `SK_BLE_MANUFACTURER`   | `clackups`     | shown in some host UIs   |
| `SK_BLE_BOND_ON_PAIR`   | y              | persist bonds to NVS     |

## USB HID

Only visible when `SK_HID_TRANSPORT_USB` is selected.

| Option                  | Default        | Purpose                  |
| ----------------------- | -------------- | ------------------------ |
| `SK_USB_MANUFACTURER`   | `clackups`     | USB iManufacturer string |
| `SK_USB_PRODUCT`        | `SmartKeyboard`| USB iProduct string      |
| `SK_USB_SERIAL`         | `000001`       | USB iSerial string       |

When building a pure-USB image you can also disable the Bluetooth
stack in `menuconfig > Component config > Bluetooth` to reclaim
the ~60 KB it occupies in flash + ~30 KB in RAM.

## Keyboard layout

`SK_LAYOUT_DEFAULT` is the compile-time default. The active layout
is **not** persisted to NVS: the device always boots with the first
*available* (Kconfig-activated) language. The active layout is
bound to the on-screen **Lng** key (right of F12), which rotates
through the *enabled* languages only (see the settings menu below).
US and Ukrainian (UA) are full layouts; German (DE) and French (FR)
are still 1x1 placeholders. The Ukrainian layout renders the real
Cyrillic letters on the keys (upper-cased while Shift is held),
using the Cyrillic glyphs embedded in the 10x20 UI font.

### Enabled languages

The **Enabled languages** Kconfig switches decide which layouts are
*available* on the device. Only available layouts are offered in the
settings menu and take part in the Lng rotation:

| Option                  | Default | Layout              |
| ----------------------- | ------- | ------------------- |
| `SK_LANG_ENABLE_US`     | y       | US English          |
| `SK_LANG_ENABLE_DE`     | n       | German (DE) stub    |
| `SK_LANG_ENABLE_FR`     | n       | French (FR) stub    |
| `SK_LANG_ENABLE_UA`     | y       | Ukrainian (UA)      |

Layouts not activated here never appear in the settings menu. The
enabled set among the available layouts can be changed at runtime
from the settings menu, but is not persisted across reboots.
`SK_LANG_ENABLE_UA` also controls whether the Ukrainian narrator
clips (`wav/ua_*.wav`) are embedded into the firmware image.

### Host layout-switch hotkey

When the device's active layout changes, the firmware emits
`Ctrl+Shift+<digit>` so a host configured with matching
input-language hotkeys switches along with it. Each language's
digit is configurable under **Host layout-switch hotkey**; `0`
disables the report for that language:

| Option                        | Default | Sends           |
| ----------------------------- | ------- | --------------- |
| `SK_LAYOUT_SWITCH_DIGIT_US`   | 1       | Ctrl+Shift+1    |
| `SK_LAYOUT_SWITCH_DIGIT_DE`   | 2       | Ctrl+Shift+2    |
| `SK_LAYOUT_SWITCH_DIGIT_FR`   | 3       | Ctrl+Shift+3    |
| `SK_LAYOUT_SWITCH_DIGIT_UA`   | 4       | Ctrl+Shift+4    |

## Theme

Same model as the layout: a Kconfig default that can be changed
at runtime from the settings menu (`keyboard_ui_cycle_theme()`).
Five built-ins ship: `green_on_black` (default),
`darkgreen_on_black`, `white_on_black`, `black_on_white`,
`default`.

## Settings menu

The on-screen **Mnu** key (right of F12) opens a modal,
gamepad-navigated settings menu. While it is open the keyboard is
replaced by the menu and the gamepad routes here: Up/Down move the
selection, Left/Right change the highlighted value, and the action
button (`GP_BTN_0`) activates it. The menu lets the user pick the
colour theme, set the **mouse pointer speed** (7 levels, slow to
fast) and toggle which of the *available* (Kconfig-activated)
languages take part in the Lng rotation. Languages not activated in
Kconfig are not listed. At least one language is always enabled and
the active language can never be disabled. Theme and mouse-speed
choices persist to NVS; the language selection does not.

## Gamepad buttons

The external gamepad's numbered buttons map to fixed actions
(`components/input_router`):

| Button     | Keyboard mode            | Mouse mode        |
| ---------- | ------------------------ | ----------------- |
| `GP_BTN_0` | press selected key       | left click        |
| `GP_BTN_1` | Shift + selected key     | right click       |
| `GP_BTN_2` | Space                    | Space             |
| `GP_BTN_3` | Enter                    | Enter             |
| `GP_BTN_4` | Backspace                | Backspace         |
| `GP_BTN_5` | Ctrl + selected key      | left click        |
| `GP_BTN_6` | AltGr + selected key     | left click        |
| `GP_BTN_7` | unused                   | unused            |
| `GP_BTN_8` | unused                   | unused            |
| `GP_BTN_9` | down: mouse mode; up: keyboard mode          |

The D-pad / analog stick moves the selection cursor (keyboard
mode) or the menu selection (settings menu). In **mouse mode** the
analog axes drive the pointer **proportionally**: the further the
stick is pushed past the dead-zone, the faster the pointer moves,
scaled by the Mouse-speed level chosen in the settings menu.
`GP_BTN_5` / `GP_BTN_6` act like `GP_BTN_0` but momentarily
hold Ctrl / AltGr for that single keypress. Shift / Ctrl / Alt /
AltGr toggled via the on-screen modifier keys are **sticky**: they
hold the modifier until the next character key is pressed.

## Audio / narrator (conditional)

| Option                | Default | Purpose                         |
| --------------------- | ------- | ------------------------------- |
| `NARRATOR_ENABLE`     | y       | speak each key as it is sent    |
| `AUDIO_I2S_PORT`      | 0       | I2S port (0 or 1)               |
| `AUDIO_I2S_BCLK_GPIO` | 5       |                                 |
| `AUDIO_I2S_LRCK_GPIO` | 6       |                                 |
| `AUDIO_I2S_DOUT_GPIO` | 7       |                                 |
| `AUDIO_VOLUME`        | 70      | 0..100, applied digitally       |

The narrator pronounces the letter or symbol that a gamepad
action sends, taking the current Shift state into account. The
Ukrainian layout ships its own clip set so the spoken name matches
the Cyrillic character the host receives, not the ASCII
transliteration drawn on screen.

Sound output is **always** I2S -- there is no PWM / internal-DAC
fallback. On boards without `BOARD_HAS_SPEAKER` selected the
audio + narrator components compile to no-op stubs so calls from
`input_router` are free.

## NVS overrides

Runtime preferences live under the `sk_ui` NVS namespace:

| Key        | Type | Set by                          |
| ---------- | ---- | ------------------------------- |
| `theme`    | str  | `keyboard_ui_cycle_theme()`     |

The active language and the enabled-language set are intentionally
not persisted: the device always boots with the first available
(Kconfig-activated) layout.

To reset to the Kconfig defaults, erase the namespace from a
serial console or wipe NVS with `idf.py erase-flash`.
