/*
 * Off-screen 32-bit drawing surface used by the custom controls.
 */
#ifndef RENDER_H
#define RENDER_H

#include <windows.h>

#include "font.h"

/* Canvas colors are 0x00RRGGBB (DIB byte order), not COLORREF. */
#define CRGB(r, g, b) ((DWORD)(((DWORD)(r) << 16) | ((DWORD)(g) << 8) | (DWORD)(b)))

typedef struct Canvas {
    HDC dc;
    HBITMAP bitmap;
    HGDIOBJ old_bitmap;
    DWORD *bits;
    int width, height;
} Canvas;

/* (Re)creates the surface if its size differs. Returns FALSE on failure. */
BOOL canvas_ensure(Canvas *c, int width, int height);
void canvas_destroy(Canvas *c);

/* Direct pixel access. Call GdiFlush() first if GDI drew on c->dc since. */
void canvas_fill(Canvas *c, int x, int y, int w, int h, DWORD color);
void canvas_frame(Canvas *c, int x, int y, int w, int h, int thickness, DWORD color);
void canvas_draw_glyph(Canvas *c, const VgaFont *font, int ch, int x, int y,
                       int scale, int nine_dot, DWORD fg, DWORD bg);

void canvas_present(Canvas *c, HDC dst);

DWORD color_from_colorref(COLORREF c);

/* Shared VGA palette entries. */
#define VGA_BLACK      CRGB(0x00, 0x00, 0x00)
#define VGA_BLUE       CRGB(0x00, 0x00, 0xAA)
#define VGA_LIGHT_GRAY CRGB(0xAA, 0xAA, 0xAA)
#define VGA_DARK_GRAY  CRGB(0x55, 0x55, 0x55)
#define VGA_YELLOW     CRGB(0xFF, 0xFF, 0x55)
#define VGA_WHITE      CRGB(0xFF, 0xFF, 0xFF)

#endif
