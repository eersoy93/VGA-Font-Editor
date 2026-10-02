/*
 * Custom child controls: glyph editor, character map and text preview.
 *
 * They draw the VgaFont the parent owns and report user actions to the
 * parent with WM_COMMAND notifications (HIWORD(wParam) = notification code).
 */
#ifndef CONTROLS_H
#define CONTROLS_H

#include <windows.h>

#include "font.h"

#define GLYPH_EDITOR_CLASS L"VgaGlyphEditor"
#define CHAR_MAP_CLASS     L"VgaCharMap"
#define PREVIEW_CLASS      L"VgaPreview"

/* Messages understood by all three controls. */
#define VFC_SETFONT    (WM_USER + 1)  /* lParam: VgaFont* (the editor modifies it) */
#define VFC_SETCHAR    (WM_USER + 2)  /* wParam: glyph index */
#define VFC_GETCHAR    (WM_USER + 3)  /* returns the glyph index */
#define VFC_SETOPTIONS (WM_USER + 4)  /* wParam: VFO_* flags */

/* Glyph editor only: returns MAKELONG(x, y) of the hovered pixel or -1. */
#define GE_GETHOVER    (WM_USER + 10)

/* Preview only. */
#define PV_SETTEXT     (WM_USER + 20) /* wParam: length, lParam: CP437 codes */
#define PV_SETSCALE    (WM_USER + 21) /* wParam: pixel scale */

#define VFO_GRID    0x01  /* draw grid lines in the glyph editor */
#define VFO_NINEDOT 0x02  /* show glyphs 9 pixels wide like VGA text mode */

/* Notification codes. */
#define GEN_BEGINEDIT 1  /* about to modify the font (save undo state now) */
#define GEN_CHANGED   2  /* font pixels changed */
#define GEN_HOVER     3  /* hovered pixel changed */
#define CMN_SELCHANGE 4  /* character map selection changed */

BOOL glyph_editor_register(HINSTANCE inst);
BOOL char_map_register(HINSTANCE inst);
BOOL preview_register(HINSTANCE inst);

/* Size a character map needs to show all glyphs at the given scale. */
void char_map_measure(int glyph_height, int nine_dot, int scale, int *width, int *height);

#endif
