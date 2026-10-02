/*
 * Rasterizes an installed Windows font into the 8-pixel-wide glyph cells.
 */
#ifndef SYSFONT_H
#define SYSFONT_H

#include <windows.h>

#include "font.h"

/*
 * Replaces all glyphs of font with the face described by lf (face name,
 * weight, italic, charset), scaled to 8 x font->height pixels. Code points
 * follow CP437; OEM charset fonts such as "Terminal" are drawn by code.
 */
BOOL sysfont_render(VgaFont *font, const LOGFONTW *lf);

#endif
