# Agent guidelines for esp32_touch_controller

These instructions apply to any automated agent or contributor making
changes in this repository.

## ASCII-only source

Only 7-bit ASCII characters (byte values 0x00-0x7F) may be used anywhere
in the code and in the text files of this repository. This includes, but
is not limited to:

- C source and header files (`*.c`, `*.h`)
- Build and configuration files (`CMakeLists.txt`, `Kconfig*`,
  `sdkconfig.defaults`, `idf_component.yml`)
- Documentation and metadata (`*.md`, `LICENSE`)

Do not use non-ASCII characters such as smart quotes, em/en dashes,
arrows, box-drawing characters, the multiplication sign, accented
letters, or emoji. Use ASCII equivalents instead, for example:

| Instead of | Use   |
|------------|-------|
| `->`, `<-` arrows | `->`, `<-` |
| `>=`, `<=` | `>=`, `<=`  |
| box-drawing lines | `-`, `|`, `+` |
| `x` (multiply) | `x` |
| em/en dash | `--` or `-` |
| smart quotes | `'` or `"` |
| ellipsis | `...` |

### Verifying

Before committing, verify that no tracked file contains non-ASCII bytes:

```sh
git ls-files -z | xargs -0 grep -nP '[^\x00-\x7F]'
```

The command must produce no output. Any match is a violation and must be
fixed before the change is committed.

## General conventions

- Keep changes minimal and focused on the task at hand.
- Follow the existing code style (4-space indentation, C comments).
- Board-specific pin definitions live behind `CONFIG_TC_BOARD_*` guards
  in `main/main.c`; tunable behaviour is exposed through
  `main/Kconfig.projbuild`.
