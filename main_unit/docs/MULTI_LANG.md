# Adding a keyboard layout

The keyboard layout is a static `kb_layout_t` describing a
`rows x cols` grid of `kb_key_t` entries. Layouts live in
`components/kb_layout/src/layout_<lang>.c` and are registered in
`components/kb_layout/src/kb_layout.c` so that
`kb_layout_cycle_layout()` walks them in order.

## Steps

1. **Pick a short identifier** (`"US"`, `"DE"`, `"FR"`, `"UA"`,
   ...). It is used as the menu label in the status bar.

2. **Copy `layout_us.c`** to `layout_<lang>.c`.

3. **Fill in the grid.** Each entry is

   ```c
   K("a", "A", HID_USAGE_A)
   ```

   - first arg: unshifted ASCII label drawn on the cell,
   - second arg: shifted ASCII label,
   - third arg: USB HID usage ID (see `kb_layout.h` for the
     `HID_USAGE_*` constants).

   Use `KSP(...)` for special keys (Enter, Space, Backspace,
   Tab, Escape) so the renderer colours them with the modifier
   palette; use `KNONE` for empty grid slots.

   Two special keys are reserved for UI actions and carry no HID
   usage: `KB_KEY_SPECIAL_LANG` (the on-screen **Lng** key, which
   rotates through the enabled languages) and
   `KB_KEY_SPECIAL_MENU` (the **Mnu** key, which opens the
   gamepad-navigated settings menu). The reference layouts place
   them on the function-key row, just right of F12.

4. **Add the file to the component**: append it to
   `components/kb_layout/CMakeLists.txt`.

5. **Register the layout** in
   `components/kb_layout/src/kb_layout.c` by adding it to
   `s_all[]`.

6. **Surface it in Kconfig** by adding a new
   `config SK_LAYOUT_<LANG>` entry and a `config
   SK_LANG_ENABLE_<LANG>` switch to `Kconfig.projbuild`, then teach
   `lang_available_name()` in `kb_layout.c` about the new identifier
   so the layout is recognised as available when its
   `SK_LANG_ENABLE_<LANG>` switch is set.

## Non-Latin on-screen glyphs

The base 8x8 font only covers ASCII, but the higher-resolution
10x20 UI font (used for single-glyph key labels) also ships
embedded Cyrillic glyphs for the Ukrainian alphabet
(`components/fonts/src/font10x20_cyrillic.h`). To draw a real
non-ASCII letter on a key, set the `glyph` field of `kb_key_t` to
the letter's Unicode codepoint (the `KG(...)` macro in
`layout_ua.c` is the reference). The renderer draws that glyph via
`display_draw_glyph_10x20_cp()`, upper-casing it automatically
while Shift is held. Keep `label_unshifted` as a short ASCII
transliteration: it is used as a fallback when a cell is too small
for the 10x20 glyph. To extend coverage to another script, add its
glyphs to the 10x20 Cyrillic-style header (or a new sibling header)
and the lookup in `components/fonts/src/fonts.c`.

## Narrating non-Latin layouts

To make the narrator speak the real character, give each key
explicit narrator clip tokens via the `sound_unshifted` /
`sound_shifted` fields of `kb_key_t` (the `KS(...)` / `KG(...)`
macros in `layout_ua.c` are the reference). A token names a WAV
file under `components/narrator/wav/<token>.wav` and must be
registered in the `S_TOKEN_CLIPS[]` table in
`components/narrator/src/narrator.c`. The Ukrainian layout keys
its clips by Unicode codepoint (e.g. `ua_u0430` for the letter
`a`). When a key carries no token the narrator falls back to the
language-neutral clip resolved from its HID usage.

Language-specific clips are only embedded when their layout is
enabled. Name them with a unique `wav/<lang>_*.wav` prefix and gate
both their `WAV_SYMS(...)` / token-table block in `narrator.c` and
the embed filter in `components/narrator/CMakeLists.txt` on the
layout's `CONFIG_SK_LANG_ENABLE_<LANG>` symbol (the Ukrainian
`wav/ua_*.wav` clips are the reference). See "Narrator WAV gating"
in `AGENTS.md`.

## Caveats

The base 8x8 font (`components/fonts/`) is the public-domain
font8x8 covering ASCII 0x20..0x7E; multi-character labels and the
status bar use it. Single-glyph key labels use the 10x20 font,
which additionally carries the embedded Ukrainian Cyrillic glyphs
described above. Layouts whose letters are not yet in the 10x20
font fall back to short Latin transliterations. The German (`DE`)
and French (`FR`) layouts still ship as 1x1 placeholders until they
are filled in the same way.
