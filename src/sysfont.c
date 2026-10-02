#include "sysfont.h"
#include "cp437.h"

#include <string.h>

/* Codes that are blank on a VGA no matter what the source font contains. */
static BOOL always_blank(int ch)
{
    return ch == 0x00 || ch == 0x20 || ch == 0xFF;
}

/* Shades, line drawing and blocks fill the whole cell so they connect. */
static BOOL full_width(int ch)
{
    return ch >= 0xB0 && ch <= 0xDF;
}

static HFONT create_cell_font(const LOGFONTW *base, int width, int height)
{
    LOGFONTW lf = *base;

    /* lfHeight > 0 selects by cell height (ascent + descent). */
    lf.lfHeight = height;
    lf.lfWidth = width;
    lf.lfEscapement = 0;
    lf.lfOrientation = 0;
    lf.lfQuality = NONANTIALIASED_QUALITY;
    return CreateFontIndirectW(&lf);
}

BOOL sysfont_render(VgaFont *font, const LOGFONTW *lf_in)
{
    const int w = FONT_WIDTH, h = font->height;
    BOOL oem = lf_in->lfCharSet == OEM_CHARSET;
    BITMAPINFO bmi;
    void *bits = NULL;
    HDC dc;
    HBITMAP bitmap;
    HFONT text_font, box_font;
    HGDIOBJ old_bitmap, old_font;
    int ch, x, y;

    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    dc = CreateCompatibleDC(NULL);
    if (!dc)
        return FALSE;
    bitmap = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    /* Like VGA fonts, text glyphs use 7 columns and leave the last one as spacing. */
    text_font = create_cell_font(lf_in, w - 1, h);
    box_font = create_cell_font(lf_in, w, h);
    if (!bitmap || !text_font || !box_font) {
        if (bitmap)
            DeleteObject(bitmap);
        if (text_font)
            DeleteObject(text_font);
        if (box_font)
            DeleteObject(box_font);
        DeleteDC(dc);
        return FALSE;
    }
    old_bitmap = SelectObject(dc, bitmap);
    old_font = SelectObject(dc, text_font);
    SetTextColor(dc, RGB(255, 255, 255));
    SetBkMode(dc, TRANSPARENT);
    SetTextAlign(dc, TA_TOP | TA_LEFT | TA_NOUPDATECP);

    for (ch = 0; ch < FONT_GLYPHS; ch++) {
        DWORD *px = (DWORD *)bits;
        int cell_w = full_width(ch) ? w : w - 1;
        SIZE size;

        GdiFlush();
        memset(px, 0, (size_t)w * h * sizeof(DWORD));
        glyph_clear(font, ch);
        if (always_blank(ch))
            continue;
        SelectObject(dc, full_width(ch) ? box_font : text_font);

        if (oem) {
            /* OEM fonts (e.g. Terminal) are indexed by the DOS code itself. */
            char code = (char)ch;
            GetTextExtentPoint32A(dc, &code, 1, &size);
            TextOutA(dc, (cell_w - size.cx) / 2, 0, &code, 1);
        } else {
            WCHAR wc = (WCHAR)cp437_to_unicode[ch];
            WORD index = 0;

            /* Leave glyphs the face does not have blank rather than drawing a box. */
            if (GetGlyphIndicesW(dc, &wc, 1, &index, GGI_MARK_NONEXISTING_GLYPHS) != GDI_ERROR &&
                index == 0xFFFF)
                continue;
            GetTextExtentPoint32W(dc, &wc, 1, &size);
            TextOutW(dc, (cell_w - size.cx) / 2, 0, &wc, 1);
        }
        GdiFlush();

        for (y = 0; y < h; y++)
            for (x = 0; x < cell_w; x++)
                if (((px[y * w + x] >> 8) & 0xFF) >= 0x80)
                    font_set_pixel(font, ch, x, y, 1);
    }

    SelectObject(dc, old_font);
    SelectObject(dc, old_bitmap);
    DeleteObject(text_font);
    DeleteObject(box_font);
    DeleteObject(bitmap);
    DeleteDC(dc);
    return TRUE;
}
