/*
 * Preview control: renders sample text with the font, VGA style.
 */
#include "controls.h"
#include "render.h"

#include <stdlib.h>
#include <string.h>

#define PREVIEW_MAX_TEXT 1024

typedef struct Preview {
    const VgaFont *font;
    unsigned options;
    int scale;
    unsigned char text[PREVIEW_MAX_TEXT];
    int length;
    Canvas canvas;
} Preview;

static void paint(HWND hwnd, Preview *pv)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;

    GetClientRect(hwnd, &rc);
    if (!canvas_ensure(&pv->canvas, rc.right, rc.bottom)) {
        EndPaint(hwnd, &ps);
        return;
    }
    GdiFlush();
    canvas_fill(&pv->canvas, 0, 0, rc.right, rc.bottom, VGA_BLACK);
    if (pv->font) {
        int base_w = (pv->options & VFO_NINEDOT) ? FONT_WIDTH + 1 : FONT_WIDTH;
        int cw = base_w * pv->scale, chh = pv->font->height * pv->scale;
        int pad = 2 * pv->scale;
        int x = pad, y = pad, i;

        for (i = 0; i < pv->length; i++) {
            if (x + cw > rc.right - pad) {
                x = pad;
                y += chh;
            }
            if (y + chh > rc.bottom)
                break;
            canvas_draw_glyph(&pv->canvas, pv->font, pv->text[i], x, y, pv->scale,
                              pv->options & VFO_NINEDOT, VGA_LIGHT_GRAY, VGA_BLACK);
            x += cw;
        }
    }
    canvas_present(&pv->canvas, dc);
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK preview_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    Preview *pv = (Preview *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_NCCREATE:
        pv = (Preview *)calloc(1, sizeof(*pv));
        if (!pv)
            return FALSE;
        pv->scale = 2;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pv);
        break;

    case WM_NCDESTROY:
        if (pv) {
            canvas_destroy(&pv->canvas);
            free(pv);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        break;

    case VFC_SETFONT:
        pv->font = (const VgaFont *)lp;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case VFC_SETOPTIONS:
        pv->options = (unsigned)wp;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case PV_SETTEXT:
        pv->length = (int)min(wp, PREVIEW_MAX_TEXT);
        if (pv->length > 0)
            memcpy(pv->text, (const void *)lp, (size_t)pv->length);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case PV_SETSCALE:
        pv->scale = max(1, (int)wp);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        paint(hwnd, pv);
        return 0;

    case WM_SIZE:
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

BOOL preview_register(HINSTANCE inst)
{
    WNDCLASSEXW wc;

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = preview_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = PREVIEW_CLASS;
    return RegisterClassExW(&wc) != 0;
}
