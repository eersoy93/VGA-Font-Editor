/*
 * Glyph editor control: a zoomed pixel grid of the current glyph.
 * Left button toggles/paints pixels, right button erases.
 */
#include "controls.h"
#include "render.h"

#include <windowsx.h>
#include <stdlib.h>
#include <wchar.h>

#define INK         CRGB(0x20, 0x20, 0x20)
#define PAPER       CRGB(0xFF, 0xFF, 0xFF)
#define GHOST_INK   CRGB(0x90, 0x90, 0xA8)
#define GHOST_PAPER CRGB(0xE4, 0xE4, 0xEC)
#define GRID_LINE   CRGB(0xC8, 0xC8, 0xC8)
#define BORDER      CRGB(0x70, 0x70, 0x70)
#define HOVER       CRGB(0x33, 0x99, 0xFF)

typedef struct GlyphEditor {
    VgaFont *font;
    int ch;
    unsigned options;
    int hover_x, hover_y;     /* -1 when no pixel is hovered */
    BOOL tracking_leave;
    UINT paint_button;        /* WM_LBUTTONDOWN/WM_RBUTTONDOWN while painting, else 0 */
    int paint_value;
    BOOL stroke_changed;      /* GEN_BEGINEDIT already sent for this stroke */
    int last_x, last_y;
    HFONT label_font;
    int label_w, label_h;
    Canvas canvas;
    /* Layout, see layout(). */
    int cols, cell, ox, oy;
    BOOL show_labels;
} GlyphEditor;

static void notify(HWND hwnd, UINT code)
{
    SendMessageW(GetParent(hwnd), WM_COMMAND,
                 MAKEWPARAM(GetDlgCtrlID(hwnd), code), (LPARAM)hwnd);
}

static int floor_div(int a, int b)
{
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

static void measure_labels(HWND hwnd, GlyphEditor *ge)
{
    HDC dc = GetDC(hwnd);
    HGDIOBJ old = SelectObject(dc, ge->label_font ? (HGDIOBJ)ge->label_font
                                                  : GetStockObject(DEFAULT_GUI_FONT));
    SIZE sz;

    GetTextExtentPoint32W(dc, L"0xFF", 4, &sz);
    ge->label_w = sz.cx;
    ge->label_h = sz.cy;
    SelectObject(dc, old);
    ReleaseDC(hwnd, dc);
}

static void layout(HWND hwnd, GlyphEditor *ge)
{
    RECT rc;
    int rows = ge->font ? ge->font->height : 16;
    int pad = ge->label_h / 2 + 2;
    int avail_w, avail_h, grid_w, grid_h, total_w;

    GetClientRect(hwnd, &rc);
    ge->cols = (ge->options & VFO_NINEDOT) ? FONT_WIDTH + 1 : FONT_WIDTH;
    avail_h = rc.bottom - 2 * pad;

    /* Prefer room for the row value labels; drop them if cells get too small. */
    avail_w = rc.right - 3 * pad - ge->label_w;
    ge->cell = min(avail_w / ge->cols, avail_h / rows);
    ge->show_labels = ge->cell >= ge->label_h;
    if (!ge->show_labels) {
        avail_w = rc.right - 2 * pad;
        ge->cell = min(avail_w / ge->cols, avail_h / rows);
    }
    if (ge->cell < 2)
        ge->cell = 2;

    grid_w = ge->cols * ge->cell + 1;
    grid_h = rows * ge->cell + 1;
    total_w = grid_w + (ge->show_labels ? pad + ge->label_w : 0);
    ge->ox = (rc.right - total_w) / 2;
    ge->oy = (rc.bottom - grid_h) / 2;
}

/* Converts client coordinates to (possibly out of range) pixel coordinates. */
static void to_pixel(const GlyphEditor *ge, int mx, int my, int *px, int *py)
{
    *px = floor_div(mx - ge->ox, ge->cell);
    *py = floor_div(my - ge->oy, ge->cell);
}

static BOOL editable_pixel(const GlyphEditor *ge, int px, int py)
{
    return ge->font && px >= 0 && px < FONT_WIDTH && py >= 0 && py < ge->font->height;
}

static void paint(HWND hwnd, GlyphEditor *ge)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    int x, y, rows, grid_w, grid_h, cell, inset;

    GetClientRect(hwnd, &rc);
    if (!canvas_ensure(&ge->canvas, rc.right, rc.bottom)) {
        EndPaint(hwnd, &ps);
        return;
    }
    GdiFlush();
    canvas_fill(&ge->canvas, 0, 0, rc.right, rc.bottom,
                color_from_colorref(GetSysColor(COLOR_BTNFACE)));
    if (!ge->font)
        goto done;

    layout(hwnd, ge);
    rows = ge->font->height;
    cell = ge->cell;
    grid_w = ge->cols * cell + 1;
    grid_h = rows * cell + 1;
    inset = (ge->options & VFO_GRID) && cell >= 4 ? 1 : 0;

    canvas_fill(&ge->canvas, ge->ox, ge->oy, grid_w, grid_h, GRID_LINE);
    for (y = 0; y < rows; y++) {
        for (x = 0; x < ge->cols; x++) {
            BOOL on = font_display_pixel(ge->font, ge->ch, x, y,
                                         ge->options & VFO_NINEDOT);
            BOOL ghost = x >= FONT_WIDTH;
            DWORD color = on ? (ghost ? GHOST_INK : INK) : (ghost ? GHOST_PAPER : PAPER);

            canvas_fill(&ge->canvas, ge->ox + x * cell + inset, ge->oy + y * cell + inset,
                        cell + 1 - 2 * inset, cell + 1 - 2 * inset, color);
        }
    }
    canvas_frame(&ge->canvas, ge->ox, ge->oy, grid_w, grid_h, 1, BORDER);
    if (ge->hover_x >= 0)
        canvas_frame(&ge->canvas, ge->ox + ge->hover_x * cell, ge->oy + ge->hover_y * cell,
                     cell + 1, cell + 1, cell >= 8 ? 2 : 1, HOVER);

    if (ge->show_labels) {
        HGDIOBJ old = SelectObject(ge->canvas.dc, ge->label_font ? (HGDIOBJ)ge->label_font
                                                                 : GetStockObject(DEFAULT_GUI_FONT));
        int lx = ge->ox + grid_w + ge->label_h / 2 + 2;

        SetBkMode(ge->canvas.dc, TRANSPARENT);
        SetTextColor(ge->canvas.dc, GetSysColor(COLOR_BTNTEXT));
        for (y = 0; y < rows; y++) {
            WCHAR text[8];
            swprintf(text, 8, L"0x%02X", ge->font->rows[ge->ch][y]);
            TextOutW(ge->canvas.dc, lx, ge->oy + y * cell + (cell - ge->label_h) / 2 + 1,
                     text, (int)wcslen(text));
        }
        SelectObject(ge->canvas.dc, old);
    }

done:
    canvas_present(&ge->canvas, dc);
    EndPaint(hwnd, &ps);
}

static void set_hover(HWND hwnd, GlyphEditor *ge, int px, int py)
{
    if (!editable_pixel(ge, px, py))
        px = py = -1;
    if (px == ge->hover_x && py == ge->hover_y)
        return;
    ge->hover_x = px;
    ge->hover_y = py;
    InvalidateRect(hwnd, NULL, FALSE);
    notify(hwnd, GEN_HOVER);
}

/* Paints a line of pixels from the last position (Bresenham) so fast drags leave no gaps. */
static void paint_to(HWND hwnd, GlyphEditor *ge, int px, int py)
{
    int x = ge->last_x, y = ge->last_y;
    int dx = abs(px - x), dy = -abs(py - y);
    int sx = x < px ? 1 : -1, sy = y < py ? 1 : -1;
    int err = dx + dy;
    BOOL changed = FALSE;

    for (;;) {
        if (editable_pixel(ge, x, y) &&
            font_get_pixel(ge->font, ge->ch, x, y) != ge->paint_value) {
            /* Only strokes that change something create an undo step. */
            if (!ge->stroke_changed) {
                ge->stroke_changed = TRUE;
                notify(hwnd, GEN_BEGINEDIT);
            }
            font_set_pixel(ge->font, ge->ch, x, y, ge->paint_value);
            changed = TRUE;
        }
        if (x == px && y == py)
            break;
        if (2 * err >= dy) { err += dy; x += sx; }
        if (2 * err <= dx) { err += dx; y += sy; }
    }
    ge->last_x = px;
    ge->last_y = py;
    if (changed) {
        InvalidateRect(hwnd, NULL, FALSE);
        notify(hwnd, GEN_CHANGED);
    }
}

static LRESULT CALLBACK glyph_editor_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    GlyphEditor *ge = (GlyphEditor *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    int px, py;

    switch (msg) {
    case WM_NCCREATE:
        ge = (GlyphEditor *)calloc(1, sizeof(*ge));
        if (!ge)
            return FALSE;
        ge->hover_x = ge->hover_y = -1;
        ge->options = VFO_GRID;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)ge);
        measure_labels(hwnd, ge);
        break;

    case WM_NCDESTROY:
        if (ge) {
            canvas_destroy(&ge->canvas);
            free(ge);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        break;

    case WM_SETFONT:
        ge->label_font = (HFONT)wp;
        measure_labels(hwnd, ge);
        if (LOWORD(lp))
            InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case VFC_SETFONT:
        ge->font = (VgaFont *)lp;
        ge->hover_x = ge->hover_y = -1;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case VFC_SETCHAR:
        ge->ch = (int)wp & 0xFF;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case VFC_GETCHAR:
        return ge->ch;

    case VFC_SETOPTIONS:
        ge->options = (unsigned)wp;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case GE_GETHOVER:
        return ge->hover_x >= 0 ? MAKELONG(ge->hover_x, ge->hover_y) : -1;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        paint(hwnd, ge);
        return 0;

    case WM_SIZE:
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
        SetFocus(GetParent(hwnd));
        if (!ge->font || ge->paint_button)
            return 0;
        layout(hwnd, ge);
        to_pixel(ge, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &px, &py);
        if (!editable_pixel(ge, px, py))
            return 0;
        ge->stroke_changed = FALSE;
        ge->paint_button = msg;
        ge->paint_value = msg == WM_LBUTTONDOWN ? !font_get_pixel(ge->font, ge->ch, px, py) : 0;
        ge->last_x = px;
        ge->last_y = py;
        SetCapture(hwnd);
        paint_to(hwnd, ge, px, py);
        return 0;

    case WM_MOUSEMOVE:
        if (!ge->font)
            return 0;
        if (!ge->tracking_leave) {
            TRACKMOUSEEVENT tme;
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            tme.dwHoverTime = 0;
            ge->tracking_leave = TrackMouseEvent(&tme);
        }
        layout(hwnd, ge);
        to_pixel(ge, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &px, &py);
        set_hover(hwnd, ge, px, py);
        if (ge->paint_button)
            paint_to(hwnd, ge, px, py);
        return 0;

    case WM_MOUSELEAVE:
        ge->tracking_leave = FALSE;
        set_hover(hwnd, ge, -1, -1);
        return 0;

    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
        if ((msg == WM_LBUTTONUP && ge->paint_button == WM_LBUTTONDOWN) ||
            (msg == WM_RBUTTONUP && ge->paint_button == WM_RBUTTONDOWN))
            ReleaseCapture();
        return 0;

    case WM_CAPTURECHANGED:
        ge->paint_button = 0;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

BOOL glyph_editor_register(HINSTANCE inst)
{
    WNDCLASSEXW wc;

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = glyph_editor_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_CROSS);
    wc.lpszClassName = GLYPH_EDITOR_CLASS;
    return RegisterClassExW(&wc) != 0;
}
