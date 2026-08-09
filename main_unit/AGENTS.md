# AGENTS.md

Guidance for AI coding agents (and humans) working in this
repository. Read this before making changes -- it captures the
ground rules and the mental model of the project that are easy to
get wrong from a fresh read of the code.

## Project at a glance

ESP-IDF firmware that turns an ESP32-family board with an on-board
LCD into a **virtual keyboard + mouse HID peripheral**. The user
navigates an on-screen ortholinear keyboard with an external
gamepad (receive-only UART HID link) and the firmware emits HID
reports to the host PC over **Bluetooth LE** (NimBLE) or **USB**
(TinyUSB). The
transport is a build-time Kconfig choice gated by what the
selected SoC actually supports. With PSRAM + a speaker, an
optional "narrator" speaks each navigated letter.

It is the ESP-IDF re-implementation of
[clackups/smart-keyboard](https://github.com/clackups/smart-keyboard)
(originally Rust on RP2040 + SH1106 OLED). Keep behavioural
parity with that project whenever the platform allows -- the
keyboard layouts, button mappings and narrator vocabulary all
come from there.

Default hardware: **Freenove FNK0104A** (2.8" 240x320 ILI9341 SPI
panel, NonTouch, ESP32-S3 with PSRAM, on-board ES8311 codec +
speaker).

## Architecture / component map

Each subsystem is an independent IDF component under
`components/` with a small public header in its `include/`
directory and the implementation in `src/`. Hardware variants are
pure Kconfig choices -- there are no `#ifdef CONFIG_BOARD_*`
ladders outside `components/board/`.

```
main/                  app entry, splash, task wiring
components/
  board/               per-board pin / capability HAL (board.h, board_t)
  display/             ILI9341 framebuffer + draw API (set_pixel,
                       fill_rect, draw_char, draw_string, flush)
  fonts/               embedded 8x8 bitmap font (public-domain
                       font8x8) + scaled-glyph helpers
  theme/               color palette table (default green-on-black)
  kb_layout/           keyboard layouts (US default, UA)
  keyboard_ui/         virtual-keyboard state machine + rendering;
                       owns selection cursor, modifier latches,
                       status bar, mouse-mode overlay
  gamepad_uart/        receive-only UART poller + 6-byte HID-report
                       parser (10 buttons + two 16-bit axes)
  input_router/        gamepad events -> UI nav + HID dispatch
  hid/                 transport-agnostic HID facade (hid_send_key,
                       hid_send_mouse)
  ble_hid/             NimBLE GATT HID backend (keyboard + mouse)
  usb_hid/             TinyUSB composite HID backend (keyboard + mouse)
  audio/               I2S WAV player (I2S-only sound output)
  narrator/            letter-name playback (EMBED_FILES WAVs)
docs/
  HARDWARE.md          per-board pinouts, gamepad wiring, power
  CONFIGURATION.md     Kconfig options + NVS overrides
  MULTI_LANG.md        how to add a new keyboard layout
```

Boot order (see `main/main.c`): `nvs_flash_init` -> `board_init`
-> `display_init` -> splash -> `keyboard_ui_init` ->
`keyboard_ui_redraw_now` (so the keyboard is on screen before any
slow peripheral init) -> `hid_init` -> `narrator_init` ->
`gamepad_uart_start` -> `input_router_start` ->
`keyboard_ui_start_task`.

## Build / flash / debug

```sh
. $IDF_PATH/export.sh
idf.py set-target esp32s3
idf.py menuconfig          # SMART KEYBOARD -> board, layout, theme,
                           # gamepad, HID transport
idf.py build flash monitor
```

Requires **ESP-IDF v5.3+** (NimBLE host, managed `esp_lcd`,
`esp_tinyusb`). Targets covered: `esp32`, `esp32s2`, `esp32s3`,
`esp32p4` -- see `CONFIGURATION.md` for which HID transports each
SoC exposes.

There is currently no automated test or lint suite. Validate
changes by:

1. Building for `esp32s3` with the default board
   (`CONFIG_SK_BOARD_FREENOVE_FNK0104A=y`).
2. If touching the HID transport, building once with
   `CONFIG_SK_HID_TRANSPORT_BLE=y` and once with
   `CONFIG_SK_HID_TRANSPORT_USB=y` (the relevant transport
   component is then non-empty -- see "Transport-gated TUs" below).
3. For pure UI / rendering changes, a host-side compile of the
   touched component with stub ESP-IDF headers is sufficient.

## Coding rules

### ASCII-only source code (MANDATORY)

**All files checked into this repository -- source, headers,
build files, Kconfig, Markdown documentation, configuration
defaults -- must contain only 7-bit ASCII characters (bytes
`0x09`, `0x0A`, `0x0D`, `0x20`..`0x7E`).** No UTF-8, no Latin-1,
no smart quotes, no em-dashes, no non-breaking spaces, no emoji,
no Unicode arrows or box-drawing -- not in code, not in comments,
not in string literals, not in docs.

Rationale:

- The embedded bitmap font (`components/fonts/font8x8_basic.h`)
  only covers ASCII 0x20..0x7E. Non-ASCII bytes in user-visible
  strings render as the `?` fallback glyph at best and as random
  pixels at worst.
- Several ESP-IDF tools (Kconfig parser, `idf.py menuconfig`,
  partition table generator) misbehave or warn on non-ASCII input.
- Diffs and grep stay clean across editors and locales.

Allowed exceptions (these are the only ones):

- Pre-compiled binary assets under `components/narrator/wav/`
  (they are `EMBED_FILES`'d WAV data, not source code).
- Any `.git/` content (not part of the working tree).

In comments and docs use plain-ASCII substitutes:

| Forbidden            | Use instead         |
| -------------------- | ------------------- |
| en-dash, em-dash     | `-` or `--`         |
| curly quotes         | `'` and `"`         |
| `x` multiplication   | `x` (the letter)    |
| `->` arrow (U+2192)  | `->`                |
| `<=` `>=` (U+2264-5) | `<=` `>=`           |
| non-breaking space   | regular space       |
| ellipsis (U+2026)    | `...`               |
| degree, micro, etc.  | spell it out        |

User-visible icons (e.g. arrow keys, Space, Enter, Backspace on
the virtual keyboard) are drawn as geometric primitives via
`display_set_pixel` / `display_fill_rect` in
`components/keyboard_ui/src/keyboard_ui.c`, **not** by encoding
Unicode glyphs into source strings. Add new icons by extending
`key_uses_icon()` and `draw_key_icon()` in the same file.

To verify your patch:

```sh
git ls-files \
  | grep -vE '^components/narrator/wav/' \
  | xargs grep -lP '[^\x00-\x7F]' || echo "OK: ASCII clean"
```

The command must print `OK: ASCII clean`. Any file it lists must
be fixed before commit.

### Other conventions

- Keep each subsystem in its own `components/<name>/` directory
  with a single public header in `include/<name>.h`.
- Hardware specifics live in `components/board/`. New boards add
  a `board_<model>.c` and a Kconfig `select` of the appropriate
  `BOARD_HAS_*` capability symbols (`BOARD_HAS_PSRAM`,
  `BOARD_HAS_DISPLAY*`, `BOARD_HAS_SPEAKER`, `BOARD_HAS_BATTERY`).
- Use `ESP_LOGI/D/W/E` with a per-file `static const char *TAG`
  (already established in existing components).
- Persist user-visible runtime choices (layout, theme) to NVS so
  power-cycling preserves them. The `keyboard_ui_cycle_*` helpers
  are the reference pattern.
- Bitmap drawing is software, framebuffer-based, no LVGL. Add
  glyphs by extending the 8x8 font or by drawing primitives, not
  by pulling in a vector font stack.
- C code style follows the rest of the tree: 4-space indent, K&R
  braces, comment blocks above functions for non-trivial logic,
  short single-purpose static helpers.

### Transport-gated translation units

The HID backends (`ble_hid`, `usb_hid`) compile to empty TUs when
their transport is not selected:

- `CMakeLists.txt` is **static** -- always declares its sources
  and `PRIV_REQUIRES`.
- The entire `.c` body is wrapped in `#if CONFIG_SK_HID_TRANSPORT_*`.
- Optional dependencies (e.g. `bt` for NimBLE) are gated in CMake
  via `BUILD_COMPONENTS` membership, not custom `CONFIG_*` flags,
  because ESP-IDF >= 5.5's static include checker does not
  evaluate `#if`.

Use the same pattern for any future optional component.

### Narrator / EMBED_FILES

WAV references in `components/narrator/src/narrator.c` must be
**strong** externs -- not `__attribute__((weak))` -- or the
linker drops the embed objects from the static archive and
narration goes silent without an error.

### Narrator WAV gating (only embed enabled-layout clips)

The narrator must only embed WAV clips that belong to an **enabled**
keyboard layout. Language-neutral clips (Latin letters, digits,
punctuation, modifiers, F-keys, navigation/arrow keys) are always
embedded. Language-specific clips are named with a per-language
prefix (Ukrainian uses `wav/ua_*.wav`) and are embedded only when
that layout is enabled via its `CONFIG_SK_LANG_ENABLE_*` Kconfig
symbol.

Keep these two sides in lock-step:

- `components/narrator/CMakeLists.txt` filters the embedded file
  list (for example `list(FILTER _wavs EXCLUDE REGEX "wav/ua_")`
  when `CONFIG_SK_LANG_ENABLE_UA` is off).
- `components/narrator/src/narrator.c` wraps that language's
  `WAV_SYMS(...)` externs and its token table under the same
  `#if CONFIG_SK_LANG_ENABLE_*` gate, because the embed symbols
  only exist when the clips are embedded (strong externs to a
  missing object are a link error).

When adding a new non-Latin layout, add its clips under a unique
`wav/<lang>_*.wav` prefix and gate both files on the layout's
`CONFIG_SK_LANG_ENABLE_<LANG>` symbol.

## Where to make common changes

| Task                                  | Touch these files                                     |
| ------------------------------------- | ----------------------------------------------------- |
| Add a keyboard layout                 | `components/kb_layout/src/layout_<lang>.c`, `Kconfig` |
| Add a board                           | `components/board/src/board_<model>.c`, `Kconfig`     |
| Change on-screen icons / fonts        | `components/keyboard_ui/src/keyboard_ui.c`,           |
|                                       | `components/fonts/`                                   |
| Adjust gamepad button mapping         | `components/input_router/src/input_router.c`          |
| Tweak HID report descriptors          | `components/hid/`, `components/ble_hid/`,             |
|                                       | `components/usb_hid/`                                 |
| Add a narrator clip                   | `components/narrator/wav/<name>.wav` +                |
|                                       | the EMBED_FILES + WAV_SYMS lists in `narrator.c`      |
| Document a new option                 | `docs/CONFIGURATION.md` and / or `docs/HARDWARE.md`   |

## Don'ts

- Do not commit non-ASCII bytes in source / docs / build files
  (see "ASCII-only source code" above).
- Do not introduce LVGL, NanoVG, or any other heavy graphics
  stack -- the renderer is intentionally tiny and direct.
- Do not split a component's `CMakeLists.txt` conditional on a
  custom `CONFIG_*` flag while leaving its sources unconditional;
  the include checker will complain. Keep the same gate in both
  places, or make the source body conditional and `CMakeLists.txt`
  static.
- Do not rely on Kconfig `select` inside an ESP-IDF `choice`
  (e.g. `BT_HOST` family) -- it is silently ignored. Use
  `sdkconfig.defaults` plus an explicit CMake assertion.
- Do not enable `CONFIG_TINYUSB_*` classes without bumping their
  per-class count (`CONFIG_TINYUSB_HID_COUNT`, etc.); they default
  to 0 and the class code is then not compiled in.
