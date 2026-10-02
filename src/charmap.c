/*
 * Character map control: all 256 glyphs in a 16x16 grid, click to select.
 */
#include "controls.h"
#include "render.h"

#include <windowsx.h>
#include <stdlib.h>

#define GRID_LINE CRGB(0x40, 0x40, 0x40)

typedef struct CharMap {
    const VgaFont *font;
    int sel;
    unsigned options;
    BOOL selecting;
    Canvas canvas;
    /* Layout, see layout(). */
    int scale, cell_w, cell_h, ox, oy;
} CharMap;

static void notify(HWND hwnd, UINT code)
{
    SendMessageW(GetParent(hwnd), WM_COMMAND,
                 MAKEWPARAM(GetDlgCtrlID(hwnd), code), (LPARAM)hwnd);
}

void char_map_measure(int glyph_height, int nine_dot, int scale, int *width, int *height)
{
    int base_w = nine_dot ? FONT_WIDTH + 1 : FONT_WIDTH;

    *width = 16 * base_w * scale + 17;
    *height = 16 * glyph_height * scale + 17;
}

static void layout(HWND hwnd, CharMap *cm)
{
    RECT rc;
    int base_w = (cm->options & VFO_NINEDOT) ? FONT_WIDTH + 1 : FONT_WIDTH;
    int h = cm->font ? cm->font->height : 16;
    int total_w, total_h;

    GetClientRect(hwnd, &rc);
    cm->scale = min((rc.right - 17) / (16 * base_w), (rc.bottom - 17) / (16 * h));
    if (cm->scale < 1)
        cm->scale = 1;
    cm->cell_w = base_w * cm->scale;
    cm->cell_h = h * cm->scale;
    total_w = 16 * cm->cell_w + 17;
    total_h = 16 * cm->cell_h + 17;
    cm->ox = max(0, (rc.right - total_w) / 2);
    cm->oy = max(0, (rc.bottom - total_h) / 2);
}

static int hit_test(HWND hwnd, CharMap *cm, int mx, int my)
{
    int col, row;

    layout(hwnd, cm);
    if (mx < cm->ox || my < cm->oy)
        return -1;
    col = (mx - cm->ox - 1) / (cm->cell_w + 1);
    row = (my - cm->oy - 1) / (cm->cell_h + 1);
    if (col < 0 || col > 15 || row < 0 || row > 15)
        return -1;
    return row * 16 + col;
}

static void paint(HWND hwnd, CharMap *cm)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    int i;

    GetClientRect(hwnd, &rc);
    if (!canvas_ensure(&cm->canvas, rc.right, rc.bottom)) {
        EndPaint(hwnd, &ps);
        return;
    }
    GdiFlush();
    canvas_fill(&cm->canvas, 0, 0, rc.right, rc.bottom,
                color_from_colorref(GetSysColor(COLOR_BTNFACE)));
    if (cm->font) {
        int sel_x = 0, sel_y = 0;

        layout(hwnd, cm);
        canvas_fill(&cm->canvas, cm->ox, cm->oy, 16 * cm->cell_w + 17,
                    16 * cm->cell_h + 17, GRID_LINE);
        for (i = 0; i < FONT_GLYPHS; i++) {
            int x = cm->ox + 1 + (i % 16) * (cm->cell_w + 1);
            int y = cm->oy + 1 + (i / 16) * (cm->cell_h + 1);

            if (i == cm->sel) {
                sel_x = x;
                sel_y = y;
                canvas_draw_glyph(&cm->canvas, cm->font, i, x, y, cm->scale,
                                  cm->options & VFO_NINEDOT, VGA_YELLOW, VGA_BLUE);
            } else {
                canvas_draw_glyph(&cm->canvas, cm->font, i, x, y, cm->scale,
                                  cm->options & VFO_NINEDOT, VGA_LIGHT_GRAY, VGA_BLACK);
            }
        }
        canvas_frame(&cm->canvas, sel_x - 1, sel_y - 1, cm->cell_w + 2, cm->cell_h + 2,
                     1, VGA_YELLOW);
    }
    canvas_present(&cm->canvas, dc);
    EndPaint(hwnd, &ps);
}

static void select_at(HWND hwnd, CharMap *cm, int mx, int my)
{
    int ch = hit_test(hwnd, cm, mx, my);

    if (ch < 0 || ch == cm->sel)
        return;
    cm->sel = ch;
    InvalidateRect(hwnd, NULL, FALSE);
    notify(hwnd, CMN_SELCHANGE);
}

static LRESULT CALLBACK char_map_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    CharMap *cm = (CharMap *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_NCCREATE:
        cm = (CharMap *)calloc(1, sizeof(*cm));
        if (!cm)
            return FALSE;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cm);
        break;

    case WM_NCDESTROY:
        if (cm) {
            canvas_destroy(&cm->canvas);
            free(cm);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        break;

    case VFC_SETFONT:
        cm->font = (const VgaFont *)lp;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case VFC_SETCHAR:
        cm->sel = (int)wp & 0xFF;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case VFC_GETCHAR:
        return cm->sel;

    case VFC_SETOPTIONS:
        cm->options = (unsigned)wp;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        paint(hwnd, cm);
        return 0;

    case WM_SIZE:
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_LBUTTONDOWN:
        SetFocus(GetParent(hwnd));
        cm->selecting = TRUE;
        SetCapture(hwnd);
        select_at(hwnd, cm, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;

    case WM_MOUSEMOVE:
        if (cm->selecting)
            select_at(hwnd, cm, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;

    case WM_LBUTTONUP:
        if (cm->selecting)
            ReleaseCapture();
        return 0;

    case WM_CAPTURECHANGED:
        cm->selecting = FALSE;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

BOOL char_map_register(HINSTANCE inst)
{
    WNDCLASSEXW wc;

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = char_map_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_HAND);
    wc.lpszClassName = CHAR_MAP_CLASS;
    return RegisterClassExW(&wc) != 0;
}
