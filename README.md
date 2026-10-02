# VGA Font Editor

A VGA font editor written in C with the Windows API. Create new VGA/EGA text-mode
fonts or edit existing ones: 256 glyphs, 8 pixels wide, 1–32 pixels tall.

Claude Code was used.

## Features

- Zoomed glyph editor with grid and per-row hex values
- Character map of all 256 glyphs (CP437 layout) and a live text preview
- Optional 9-dot view: shows glyphs as VGA text mode does, with the ninth
  column repeated for the line-drawing characters `0xC0`–`0xDF`
- Undo/redo (200 steps), copy/paste glyphs (also as `#`/`.` text art)
- Glyph tools: clear, invert, flip, shift, bold
- Change glyph height (keep top, center, keep bottom or scale)
- Render a starting font from any installed Windows font
- File formats:

  | Format | Open | Save |
  |---|---|---|
  | Raw VGA font (`.f08`, `.f14`, `.f16`, `.fnt`, `.bin`, …) | ✓ | ✓ |
  | PC Screen Font v1 (`.psf`) | ✓ | ✓ |
  | PC Screen Font v2 (`.psf`, width ≤ 8) | ✓ | ✓ |
  | C source array (`.h`, `.c`) | | Export |

  Raw files are detected by size (`256 × height` bytes). A `.fNN` extension sets
  the height for files with more or fewer than 256 glyphs.

## Building

Runs on Windows XP and later. The result is a single `vgafontedit.exe` with no
extra DLLs; build it 32-bit for 32-bit Windows.

**MSYS2 / MinGW-w64** (Windows) — use the MINGW32 or MINGW64 shell. UCRT64 and
CLANG64 builds need the Universal CRT, which Windows XP lacks.

```sh
make
```

**Cross compile from Linux:**

```sh
make CROSS=i686-w64-mingw32-     # 32-bit
make CROSS=x86_64-w64-mingw32-   # 64-bit
```

**Visual Studio** (CMake):

```sh
cmake -S . -B build
cmake --build build --config Release
```

Visual Studio 2019/2022 builds need Windows 7 or later because their runtime no
longer supports XP. For an XP build with Visual Studio, use VS 2017 with the
`v141_xp` toolset:

```sh
cmake -S . -B build -G "Visual Studio 15 2017" -A Win32 -T v141_xp
```

Unit tests for the font core (formats, transforms, undo) run on any platform:

```sh
make test
```

## Usage

```
vgafontedit.exe [font-file]
```

Fonts can also be opened by dragging them onto the window.

| Action | Input |
|---|---|
| Toggle / draw pixels | Left mouse button (drag to paint) |
| Erase pixels | Right mouse button |
| Select glyph | Click the character map, arrow keys, mouse wheel, PgUp/PgDn |
| Jump to a glyph | Type the character |
| Shift glyph | Ctrl + arrow keys |
| Undo / Redo | Ctrl+Z / Ctrl+Y |
| Copy / Cut / Paste glyph | Ctrl+C / Ctrl+X / Ctrl+V |
| Clear glyph | Del |
| Invert / Bold | Ctrl+I / Ctrl+B |
| Flip horizontal / vertical | Ctrl+Shift+H / Ctrl+Shift+V |
| Toggle grid | Ctrl+G |

To start a font from scratch, use **File › New** for a blank font, or
**Font › Render from Windows Font** to fill all glyphs from an installed font
and then fine-tune them.

## Project layout

```
src/font.c       font model, file formats, glyph transforms (portable C)
src/undo.c       undo/redo history (portable C)
src/cp437.c      CP437 <-> Unicode table (portable C)
src/main.c       main window, menus, commands, file I/O
src/glyphedit.c  glyph editor control
src/charmap.c    character map control
src/preview.c    text preview control
src/render.c     off-screen drawing helpers
src/sysfont.c    Windows font rasterizer
src/dialogs.c    New Font / Change Height dialogs
res/             resources: menu, accelerators, dialogs, icon, manifest
tests/           unit tests for the portable core
```

## Copyright and License

MIT — see [LICENSE](LICENSE).
