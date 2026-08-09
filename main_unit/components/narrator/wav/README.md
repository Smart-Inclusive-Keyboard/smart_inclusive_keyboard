# Narrator assets

This directory holds WAV files embedded into the firmware at
build time via `EMBED_FILES` (see
`components/narrator/CMakeLists.txt`). The narrator component is
**only** compiled in when the selected board has both PSRAM and
a speaker and `CONFIG_NARRATOR_ENABLE` is set; otherwise the
embed list is skipped entirely.

## Where do the WAVs come from?

They are taken verbatim from the upstream
[clackups/smart-keyboard](https://github.com/clackups/smart-keyboard)
project's `audio/` folder (MIT-licensed, same as this firmware).
The letter / digit clips are the US-English set
(`us_<letter>.wav` / `us_<digit>.wav` upstream), renamed to drop
the `us_` prefix so they match the symbol names the narrator
declares. `esc.wav` upstream is imported as `escape.wav`. The
narrator expects the following filenames:

```
a.wav, b.wav, ..., z.wav
0.wav, 1.wav, ..., 9.wav
space.wav, enter.wav, backspace.wav, tab.wav, escape.wav
shift.wav, ctrl.wav, alt.wav, altgr.wav, capslock.wav
apostrophe.wav, backslash.wav, backtick.wav, comma.wav,
equals.wav, lbracket.wav, minus.wav, period.wav,
rbracket.wav, semicolon.wav, slash.wav
f1.wav .. f12.wav
insert.wav, home.wav, pageup.wav,
delete.wav, end.wav, pagedown.wav,
up.wav, down.wav, left.wav, right.wav
```

Every cell of the on-screen US layout has a matching clip
except the "Win" / "Cmd" modifier (no upstream recording);
that key narrates as silence.

## Format

Standard RIFF/WAVE, **PCM 16-bit mono, 22050 Hz**. The I2S
driver re-clocks to the file's reported sample rate, so other
rates work too, but 22050 / 16-bit is what the upstream pack
uses and what the audio buffer sizes in `components/audio/` are
tuned for.

## License

MIT, inherited from the upstream project. See its `LICENSE`
file for the exact text.
