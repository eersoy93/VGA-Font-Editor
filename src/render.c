#include "render.h"

BOOL canvas_ensure(Canvas *c, int width, int height)
{
    BITMAPINFO bmi;
    void *bits = NULL;
    HDC screen;

    if (width < 1)
        width = 1;
    if (height < 1)
        height = 1;
    if (c->dc && c->width == width && c->height == height)
        return TRUE;
    canvas_destroy(c);

    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;  /* top-down */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    screen = GetDC(NULL);
    c->dc = CreateCompatibleDC(screen);
    c->bitmap = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, screen);
    if (!c->dc || !c->bitmap) {
        canvas_destroy(c);
        return FALSE;
    }
    c->old_bitmap = SelectObject(c->dc, c->bitmap);
    c->bits = (DWORD *)bits;
    c->width = width;
    c->height = height;
    return TRUE;
}

void canvas_destroy(Canvas *c)
{
    if (c->dc) {
        if (c->old_bitmap)
            SelectObject(c->dc, c->old_bitmap);
        DeleteDC(c->dc);
    }
    if (c->bitmap)
        DeleteObject(c->bitmap);
    ZeroMemory(c, sizeof(*c));
}

void canvas_fill(Canvas *c, int x, int y, int w, int h, DWORD color)
{
    int x1 = x + w, y1 = y + h, i, j;

    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > c->width) x1 = c->width;
    if (y1 > c->height) y1 = c->height;
    for (j = y; j < y1; j++) {
        DWORD *row = c->bits + (size_t)j * c->width;
        for (i = x; i < x1; i++)
            row[i] = color;
    }
}

void canvas_frame(Canvas *c, int x, int y, int w, int h, int t, DWORD color)
{
    canvas_fill(c, x, y, w, t, color);
    canvas_fill(c, x, y + h - t, w, t, color);
    canvas_fill(c, x, y, t, h, color);
    canvas_fill(c, x + w - t, y, t, h, color);
}

void canvas_draw_glyph(Canvas *c, const VgaFont *font, int ch, int x, int y,
                       int scale, int nine_dot, DWORD fg, DWORD bg)
{
    int cols = nine_dot ? FONT_WIDTH + 1 : FONT_WIDTH;
    int row, col;

    for (row = 0; row < font->height; row++)
        for (col = 0; col < cols; col++)
            canvas_fill(c, x + col * scale, y + row * scale, scale, scale,
                        font_display_pixel(font, ch, col, row, nine_dot) ? fg : bg);
}

void canvas_present(Canvas *c, HDC dst)
{
    BitBlt(dst, 0, 0, c->width, c->height, c->dc, 0, 0, SRCCOPY);
}

DWORD color_from_colorref(COLORREF c)
{
    return CRGB(GetRValue(c), GetGValue(c), GetBValue(c));
}
