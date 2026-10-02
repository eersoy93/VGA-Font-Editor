/*
 * VGA Font Editor - main window, commands and file handling.
 */
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#include "controls.h"
#include "cp437.h"
#include "dialogs.h"
#include "font.h"
#include "resource.h"
#include "sysfont.h"
#include "undo.h"

#define APP_NAME      L"VGA Font Editor"
#define MAIN_CLASS    L"VgaFontEditorMain"
#define UNDO_LIMIT    200
#define MAX_FILE_SIZE (4 * 1024 * 1024)
#define SAMPLE_TEXT   L"The quick brown fox jumps over the lazy dog. 0123456789 !?#$%&@*+-=/<>[](){}"

enum {
    IDC_EDITOR = 100,
    IDC_CHARMAP,
    IDC_PREVIEW,
    IDC_SAMPLE_LABEL,
    IDC_SAMPLE,
    IDC_STATUS
};

/* Private clipboard format: one glyph with its height. */
typedef struct ClipGlyph {
    int height;
    unsigned char rows[FONT_MAX_HEIGHT];
} ClipGlyph;

static struct App {
    HINSTANCE inst;
    HWND wnd, editor, charmap, preview, sample_label, sample, status;
    HACCEL accel;
    HFONT ui_font;
    int ui_text_h, label_w;
    int dpi;
    UINT clip_format;

    VgaFont font;
    int cur;                  /* selected glyph */
    BOOL dirty;
    WCHAR path[MAX_PATH];     /* empty for an untitled font */
    FontFormat format;        /* format used by File > Save */
    BOOL show_grid, nine_dot;
    UndoHistory undo;
} app;

/* ------------------------------------------------------------------------ */
/* Helpers                                                                  */

static int scale_px(int v)
{
    return MulDiv(v, app.dpi, 96);
}

static unsigned view_options(void)
{
    return (app.show_grid ? VFO_GRID : 0) | (app.nine_dot ? VFO_NINEDOT : 0);
}

static const WCHAR *file_name_part(const WCHAR *path)
{
    const WCHAR *p = path, *name = path;

    for (; *p; p++)
        if (*p == L'\\' || *p == L'/' || *p == L':')
            name = p + 1;
    return name;
}

static const WCHAR *document_name(void)
{
    return app.path[0] ? file_name_part(app.path) : L"Untitled";
}

/* Height from a ".fNN" extension (e.g. ".f16"), or 0. */
static int height_hint_from_path(const WCHAR *path)
{
    const WCHAR *ext = wcsrchr(file_name_part(path), L'.');
    int h;

    if (!ext || (ext[1] != L'f' && ext[1] != L'F'))
        return 0;
    if (!iswdigit(ext[2]) || !iswdigit(ext[3]) || ext[4] != L'\0')
        return 0;
    h = (ext[2] - L'0') * 10 + (ext[3] - L'0');
    return (h >= FONT_MIN_HEIGHT && h <= FONT_MAX_HEIGHT) ? h : 0;
}

static void error_box(const WCHAR *what, const WCHAR *path, DWORD err)
{
    WCHAR sys[512] = L"", msg[MAX_PATH + 1024];

    if (err)
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
                       err, 0, sys, (DWORD)(sizeof(sys) / sizeof(sys[0])), NULL);
    swprintf(msg, sizeof(msg) / sizeof(msg[0]), L"%ls\n\n%ls%ls%ls", what,
             path ? path : L"", sys[0] ? L"\n\n" : L"", sys);
    MessageBoxW(app.wnd, msg, APP_NAME, MB_OK | MB_ICONERROR);
}

static const WCHAR *font_error_text(FontError e)
{
    switch (e) {
    case FONT_ERR_UNKNOWN_FORMAT:
        return L"This is not a font file this editor understands.\n"
               L"Supported: raw VGA fonts (256 glyphs x 8 pixels wide), PSF1 and PSF2.";
    case FONT_ERR_BAD_HEIGHT:
        return L"The font's glyph height is not supported (must be 1 to 32 pixels).";
    case FONT_ERR_BAD_WIDTH:
        return L"The font's glyphs are wider than 8 pixels, which a VGA cannot display.";
    case FONT_ERR_TRUNCATED:
        return L"The font file is truncated.";
    case FONT_ERR_NO_MEMORY:
        return L"Out of memory.";
    default:
        return L"Unknown error.";
    }
}

/* ------------------------------------------------------------------------ */
/* View updates                                                             */

static void update_title(void)
{
    WCHAR title[MAX_PATH + 64];

    swprintf(title, sizeof(title) / sizeof(title[0]), L"%ls%ls - %ls",
             app.dirty ? L"*" : L"", document_name(), APP_NAME);
    SetWindowTextW(app.wnd, title);
}

static void update_status(void)
{
    static const WCHAR *const format_names[] = { L"Raw", L"PSF1", L"PSF2", L"C" };
    unsigned uc = cp437_to_unicode[app.cur];
    WCHAR text[128];
    LRESULT hover;

    if (uc > 0x20)
        swprintf(text, 128, L"Glyph %d (0x%02X)   U+%04X %lc", app.cur, app.cur, uc, (wint_t)uc);
    else
        swprintf(text, 128, L"Glyph %d (0x%02X)", app.cur, app.cur);
    SendMessageW(app.status, SB_SETTEXTW, 0, (LPARAM)text);

    swprintf(text, 128, L"%dx%d %ls", FONT_WIDTH, app.font.height, format_names[app.format]);
    SendMessageW(app.status, SB_SETTEXTW, 1, (LPARAM)text);

    hover = SendMessageW(app.editor, GE_GETHOVER, 0, 0);
    if (hover != -1) {
        int x = LOWORD(hover), y = HIWORD(hover);
        swprintf(text, 128, L"Pixel %d,%d   Row %d = 0x%02X", x, y, y, app.font.rows[app.cur][y]);
    } else {
        text[0] = L'\0';
    }
    SendMessageW(app.status, SB_SETTEXTW, 2, (LPARAM)text);
}

static void set_hint(const WCHAR *text)
{
    SendMessageW(app.status, SB_SETTEXTW, 3, (LPARAM)text);
}

static int preview_scale(void)
{
    return max(1, MulDiv(app.font.height <= 16 ? 2 : 1, app.dpi, 96));
}

static void layout(void)
{
    RECT rc, sr;
    int W, H, m, edit_h, prev_h, top_h, cm_w, cm_h, cm_x, y, s, ps, parts[4];

    if (!app.status)
        return;
    SendMessageW(app.status, WM_SIZE, 0, 0);
    GetClientRect(app.wnd, &rc);
    GetWindowRect(app.status, &sr);
    W = rc.right;
    H = rc.bottom - (sr.bottom - sr.top);
    m = scale_px(8);
    edit_h = app.ui_text_h + scale_px(8);
    ps = preview_scale();
    prev_h = 2 * app.font.height * ps + 4 * ps;
    top_h = max(scale_px(100), H - 3 * m - m / 2 - edit_h - prev_h);

    /* Largest character map scale that fits in ~60% of the width. */
    for (s = 1;; s++) {
        char_map_measure(app.font.height, app.nine_dot, s + 1, &cm_w, &cm_h);
        if (cm_w > (W - 3 * m) * 3 / 5 || cm_h > top_h)
            break;
    }
    char_map_measure(app.font.height, app.nine_dot, s, &cm_w, &cm_h);
    cm_x = W - m - cm_w;
    MoveWindow(app.charmap, cm_x, m, cm_w, min(cm_h, top_h), TRUE);
    MoveWindow(app.editor, m, m, max(1, cm_x - 2 * m), top_h, TRUE);

    y = m + top_h + m;
    MoveWindow(app.sample_label, m, y + (edit_h - app.ui_text_h) / 2, app.label_w, app.ui_text_h, TRUE);
    MoveWindow(app.sample, m + app.label_w + m, y, max(1, W - 3 * m - app.label_w), edit_h, TRUE);
    y += edit_h + m / 2;
    MoveWindow(app.preview, m, y, max(1, W - 2 * m), prev_h, TRUE);
    SendMessageW(app.preview, PV_SETSCALE, (WPARAM)ps, 0);

    parts[0] = scale_px(250);
    parts[1] = parts[0] + scale_px(90);
    parts[2] = parts[1] + scale_px(190);
    parts[3] = -1;
    SendMessageW(app.status, SB_SETPARTS, 4, (LPARAM)parts);
}

/* Redraws everything that shows font data. */
static void refresh_views(BOOL relayout)
{
    SendMessageW(app.editor, VFC_SETCHAR, (WPARAM)app.cur, 0);
    SendMessageW(app.charmap, VFC_SETCHAR, (WPARAM)app.cur, 0);
    InvalidateRect(app.preview, NULL, FALSE);
    if (relayout)
        layout();
    update_status();
    update_title();
}

static void select_glyph(int ch)
{
    app.cur = ch & 0xFF;
    SendMessageW(app.editor, VFC_SETCHAR, (WPARAM)app.cur, 0);
    SendMessageW(app.charmap, VFC_SETCHAR, (WPARAM)app.cur, 0);
    update_status();
}

/* Called by the glyph editor before it modifies app.font in place. */
static void begin_change(void)
{
    undo_push(&app.undo, &app.font, app.cur);
}

/* Records 'before' as an undo step if the font changed, then redraws. */
static void commit_change(const VgaFont *before, BOOL relayout)
{
    if (memcmp(before, &app.font, sizeof(app.font)) == 0)
        return;
    undo_push(&app.undo, before, app.cur);
    app.dirty = TRUE;
    refresh_views(relayout);
}

static void update_preview_text(void)
{
    WCHAR text[1024];
    unsigned char codes[1024];
    int n = GetWindowTextW(app.sample, text, 1024), i;

    for (i = 0; i < n; i++) {
        int c = unicode_to_cp437(text[i]);
        codes[i] = (unsigned char)(c < 0 ? '?' : c);
    }
    SendMessageW(app.preview, PV_SETTEXT, (WPARAM)n, (LPARAM)codes);
}

static void set_view_options(void)
{
    SendMessageW(app.editor, VFC_SETOPTIONS, view_options(), 0);
    SendMessageW(app.charmap, VFC_SETOPTIONS, view_options(), 0);
    SendMessageW(app.preview, VFC_SETOPTIONS, view_options(), 0);
    layout();
}

/* ------------------------------------------------------------------------ */
/* Files                                                                    */

static unsigned char *read_whole_file(const WCHAR *path, size_t *size, DWORD *err)
{
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    LARGE_INTEGER len;
    unsigned char *buf;
    DWORD got = 0;

    *err = 0;
    if (f == INVALID_HANDLE_VALUE) {
        *err = GetLastError();
        return NULL;
    }
    if (!GetFileSizeEx(f, &len)) {
        *err = GetLastError();
        CloseHandle(f);
        return NULL;
    }
    if (len.QuadPart > MAX_FILE_SIZE) {
        *err = ERROR_FILE_TOO_LARGE;
        CloseHandle(f);
        return NULL;
    }
    buf = (unsigned char *)malloc((size_t)len.QuadPart + 1);
    if (!buf) {
        *err = ERROR_NOT_ENOUGH_MEMORY;
        CloseHandle(f);
        return NULL;
    }
    if (!ReadFile(f, buf, (DWORD)len.QuadPart, &got, NULL) || got != (DWORD)len.QuadPart) {
        *err = GetLastError();
        if (!*err)
            *err = ERROR_READ_FAULT;
        free(buf);
        CloseHandle(f);
        return NULL;
    }
    CloseHandle(f);
    *size = (size_t)len.QuadPart;
    return buf;
}

static BOOL write_whole_file(const WCHAR *path, const void *data, size_t size, DWORD *err)
{
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD written = 0;
    BOOL ok;

    *err = 0;
    if (f == INVALID_HANDLE_VALUE) {
        *err = GetLastError();
        return FALSE;
    }
    ok = WriteFile(f, data, (DWORD)size, &written, NULL) && written == (DWORD)size;
    if (!ok)
        *err = GetLastError();
    if (!CloseHandle(f) && ok) {
        *err = GetLastError();
        ok = FALSE;
    }
    return ok;
}

static void show_load_warnings(unsigned warnings)
{
    WCHAR msg[1024] = L"The font was loaded with these notes:\n";

    if (!warnings)
        return;
    if (warnings & FONT_WARN_EXTRA_GLYPHS)
        wcscat(msg, L"\n- The file has more than 256 glyphs; only the first 256 were loaded.");
    if (warnings & FONT_WARN_MISSING_GLYPHS)
        wcscat(msg, L"\n- The file has fewer than 256 glyphs; the remaining ones are blank.");
    if (warnings & FONT_WARN_UNICODE_TABLE)
        wcscat(msg, L"\n- The PSF Unicode table was not loaded and will not be saved.");
    MessageBoxW(app.wnd, msg, APP_NAME, MB_OK | MB_ICONINFORMATION);
}

static BOOL open_file(const WCHAR *path)
{
    unsigned char *data;
    size_t size = 0;
    DWORD err;
    VgaFont font;
    FontFormat format;
    unsigned warnings;
    FontError fe;

    data = read_whole_file(path, &size, &err);
    if (!data) {
        error_box(L"Could not open the file.", path, err);
        return FALSE;
    }
    fe = font_parse(&font, data, size, height_hint_from_path(path), &format, &warnings);
    free(data);
    if (fe != FONT_OK) {
        error_box(font_error_text(fe), path, 0);
        return FALSE;
    }

    app.font = font;
    app.format = format;
    app.dirty = FALSE;
    lstrcpynW(app.path, path, MAX_PATH);
    undo_clear(&app.undo);
    refresh_views(TRUE);
    set_hint(L"");
    show_load_warnings(warnings);
    return TRUE;
}

/* Array name for C export: the file name without extension. */
static void c_name_from_path(const WCHAR *path, char *out, size_t cap)
{
    const WCHAR *name = file_name_part(path);
    size_t n = 0;

    for (; *name && *name != L'.' && n + 1 < cap; name++)
        out[n++] = (char)(*name < 0x80 ? *name : '_');
    out[n] = '\0';
}

static BOOL save_file(const WCHAR *path, FontFormat format)
{
    char c_name[64];
    unsigned char *data;
    size_t size = 0;
    DWORD err;
    BOOL ok;

    c_name_from_path(path, c_name, sizeof(c_name));
    data = font_serialize(&app.font, format, c_name, &size);
    if (!data) {
        error_box(font_error_text(FONT_ERR_NO_MEMORY), path, 0);
        return FALSE;
    }
    ok = write_whole_file(path, data, size, &err);
    free(data);
    if (!ok)
        error_box(L"Could not save the file.", path, err);
    return ok;
}

/* Builds a double-NUL-terminated filter string from one with '|' separators. */
static void make_filter(WCHAR *dst, size_t cap, const WCHAR *src)
{
    size_t i;

    for (i = 0; src[i] && i + 2 < cap; i++)
        dst[i] = src[i] == L'|' ? L'\0' : src[i];
    dst[i] = L'\0';
    dst[i + 1] = L'\0';
}

static BOOL cmd_save_as(void);

static BOOL cmd_save(void)
{
    if (!app.path[0])
        return cmd_save_as();
    if (!save_file(app.path, app.format))
        return FALSE;
    app.dirty = FALSE;
    update_title();
    return TRUE;
}

static BOOL cmd_save_as(void)
{
    WCHAR spec[256], filter[256], file[MAX_PATH], ext[8];
    OPENFILENAMEW ofn;

    swprintf(ext, 8, L"f%02d", app.font.height);
    swprintf(spec, 256,
             L"Raw VGA font (*.%ls;*.fnt;*.bin)|*.%ls;*.fnt;*.bin|"
             L"PC Screen Font v1 (*.psf)|*.psf|"
             L"PC Screen Font v2 (*.psf)|*.psf|", ext, ext);
    make_filter(filter, 256, spec);
    if (app.path[0])
        lstrcpynW(file, app.path, MAX_PATH);
    else
        swprintf(file, MAX_PATH, L"untitled.%ls", app.format == FONT_FORMAT_RAW ? ext : L"psf");

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = app.wnd;
    ofn.lpstrFilter = filter;
    ofn.nFilterIndex = app.format == FONT_FORMAT_PSF1 ? 2 : app.format == FONT_FORMAT_PSF2 ? 3 : 1;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = app.format == FONT_FORMAT_RAW ? ext : L"psf";
    ofn.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetSaveFileNameW(&ofn))
        return FALSE;

    app.format = ofn.nFilterIndex == 2 ? FONT_FORMAT_PSF1
               : ofn.nFilterIndex == 3 ? FONT_FORMAT_PSF2 : FONT_FORMAT_RAW;
    if (!save_file(file, app.format))
        return FALSE;
    lstrcpynW(app.path, file, MAX_PATH);
    app.dirty = FALSE;
    update_title();
    update_status();
    return TRUE;
}

static void cmd_export_c(void)
{
    WCHAR filter[128], file[MAX_PATH];
    OPENFILENAMEW ofn;
    const WCHAR *ext;

    make_filter(filter, 128, L"C header (*.h)|*.h|C source (*.c)|*.c|All files (*.*)|*.*|");
    if (app.path[0]) {
        lstrcpynW(file, app.path, MAX_PATH);
        ext = wcsrchr(file_name_part(file), L'.');
        if (ext)
            file[ext - file] = L'\0';
    } else {
        swprintf(file, MAX_PATH, L"vga_font_8x%d", app.font.height);
    }

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = app.wnd;
    ofn.lpstrFilter = filter;
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"h";
    ofn.lpstrTitle = L"Export as C Source";
    ofn.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (GetSaveFileNameW(&ofn) && save_file(file, FONT_FORMAT_C_SOURCE))
        set_hint(L"Exported as C source.");
}

/* Returns TRUE if the current font may be replaced (saved or discarded). */
static BOOL confirm_discard(void)
{
    WCHAR msg[MAX_PATH + 64];
    int r;

    if (!app.dirty)
        return TRUE;
    swprintf(msg, sizeof(msg) / sizeof(msg[0]), L"Save changes to %ls?", document_name());
    r = MessageBoxW(app.wnd, msg, APP_NAME, MB_YESNOCANCEL | MB_ICONWARNING);
    if (r == IDYES)
        return cmd_save();
    return r == IDNO;
}

static void new_font(int height)
{
    font_init(&app.font, height);
    app.path[0] = L'\0';
    app.format = FONT_FORMAT_RAW;
    app.dirty = FALSE;
    undo_clear(&app.undo);
    refresh_views(TRUE);
}

static void cmd_new(void)
{
    int height;

    if (!confirm_discard())
        return;
    height = dialog_new_font(app.wnd, app.font.height);
    if (height)
        new_font(height);
}

static void cmd_open(void)
{
    WCHAR filter[512], file[MAX_PATH] = L"";
    OPENFILENAMEW ofn;

    if (!confirm_discard())
        return;
    make_filter(filter, 512,
                L"All supported fonts|*.f??;*.fnt;*.psf;*.bin;*.rom|"
                L"Raw VGA fonts (*.f08;*.f14;*.f16;*.fnt;*.bin)|*.f??;*.fnt;*.bin;*.rom|"
                L"PC Screen Fonts (*.psf)|*.psf|"
                L"All files (*.*)|*.*|");
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = app.wnd;
    ofn.lpstrFilter = filter;
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (GetOpenFileNameW(&ofn))
        open_file(file);
}

/* ------------------------------------------------------------------------ */
/* Edit and glyph commands                                                  */

static void cmd_copy(void)
{
    WCHAR art[FONT_MAX_HEIGHT * (FONT_WIDTH + 2) + 1];
    HGLOBAL mem;

    if (!OpenClipboard(app.wnd))
        return;
    EmptyClipboard();

    mem = GlobalAlloc(GMEM_MOVEABLE, sizeof(ClipGlyph));
    if (mem) {
        ClipGlyph *cg = (ClipGlyph *)GlobalLock(mem);
        cg->height = app.font.height;
        memcpy(cg->rows, app.font.rows[app.cur], FONT_MAX_HEIGHT);
        GlobalUnlock(mem);
        if (!SetClipboardData(app.clip_format, mem))
            GlobalFree(mem);
    }

    /* Also as text art so glyphs can be pasted into documents and back. */
    glyph_to_art(&app.font, app.cur, art, sizeof(art) / sizeof(art[0]));
    mem = GlobalAlloc(GMEM_MOVEABLE, (wcslen(art) + 1) * sizeof(WCHAR));
    if (mem) {
        memcpy(GlobalLock(mem), art, (wcslen(art) + 1) * sizeof(WCHAR));
        GlobalUnlock(mem);
        if (!SetClipboardData(CF_UNICODETEXT, mem))
            GlobalFree(mem);
    }
    CloseClipboard();
}

static BOOL can_paste(void)
{
    return IsClipboardFormatAvailable(app.clip_format) ||
           IsClipboardFormatAvailable(CF_UNICODETEXT);
}

static void cmd_paste(void)
{
    unsigned char rows[FONT_MAX_HEIGHT];
    int count = 0, y;
    BOOL ok = FALSE;
    HANDLE mem;
    VgaFont before;

    if (!OpenClipboard(app.wnd))
        return;
    mem = GetClipboardData(app.clip_format);
    if (mem && GlobalSize(mem) >= sizeof(ClipGlyph)) {
        const ClipGlyph *cg = (const ClipGlyph *)GlobalLock(mem);
        if (cg) {
            memcpy(rows, cg->rows, FONT_MAX_HEIGHT);
            count = min(max(cg->height, 0), FONT_MAX_HEIGHT);
            ok = TRUE;
            GlobalUnlock(mem);
        }
    }
    if (!ok && (mem = GetClipboardData(CF_UNICODETEXT)) != NULL) {
        size_t chars = GlobalSize(mem) / sizeof(WCHAR);
        const WCHAR *text = (const WCHAR *)GlobalLock(mem);
        if (text && chars > 0 && chars < 4096) {
            WCHAR buf[4096];
            memcpy(buf, text, chars * sizeof(WCHAR));
            buf[chars] = L'\0';
            ok = glyph_from_art(buf, rows, &count);
        }
        if (text)
            GlobalUnlock(mem);
    }
    CloseClipboard();

    if (!ok) {
        MessageBeep(MB_ICONWARNING);
        set_hint(L"The clipboard does not contain a glyph.");
        return;
    }
    before = app.font;
    glyph_clear(&app.font, app.cur);
    for (y = 0; y < count && y < app.font.height; y++)
        app.font.rows[app.cur][y] = rows[y];
    commit_change(&before, FALSE);
}

static void glyph_op(void (*op)(VgaFont *, int))
{
    VgaFont before = app.font;

    op(&app.font, app.cur);
    commit_change(&before, FALSE);
}

static void shift_op(int dx, int dy)
{
    VgaFont before = app.font;

    glyph_shift(&app.font, app.cur, dx, dy);
    commit_change(&before, FALSE);
}

static void cmd_undo_redo(BOOL redo)
{
    int old_height = app.font.height;
    int ok = redo ? undo_redo(&app.undo, &app.font, &app.cur)
                  : undo_undo(&app.undo, &app.font, &app.cur);

    if (!ok)
        return;
    app.dirty = TRUE;
    refresh_views(app.font.height != old_height);
}

static void cmd_change_height(void)
{
    static FontResizeMode mode = FONT_RESIZE_TOP;
    int height = app.font.height;
    VgaFont before = app.font;

    if (!dialog_resize_font(app.wnd, app.font.height, &height, &mode) ||
        height == app.font.height)
        return;
    font_resize(&app.font, height, mode);
    commit_change(&before, TRUE);
}

static void cmd_render_font(void)
{
    static LOGFONTW lf;
    CHOOSEFONTW cf;
    VgaFont before = app.font, rendered = app.font;

    if (!lf.lfFaceName[0]) {
        lf.lfHeight = -scale_px(16);
        lf.lfWeight = FW_NORMAL;
        lf.lfCharSet = DEFAULT_CHARSET;
        lstrcpynW(lf.lfFaceName, L"Consolas", LF_FACESIZE);
    }
    ZeroMemory(&cf, sizeof(cf));
    cf.lStructSize = sizeof(cf);
    cf.hwndOwner = app.wnd;
    cf.lpLogFont = &lf;
    cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_NOVERTFONTS | CF_FORCEFONTEXIST;
    if (!ChooseFontW(&cf))
        return;

    if (!sysfont_render(&rendered, &lf)) {
        error_box(L"Could not render the selected font.", NULL, GetLastError());
        return;
    }
    app.font = rendered;
    commit_change(&before, FALSE);
    set_hint(L"Rendered all glyphs. Undo (Ctrl+Z) restores the previous ones.");
}

static void cmd_about(void)
{
    MessageBoxW(app.wnd,
        L"VGA Font Editor 1.0\n"
        L"Create and edit 8-pixel-wide VGA/EGA bitmap fonts.\n\n"
        L"Formats: raw (.f08, .f14, .f16, .fnt, ...), PSF1 and PSF2.\n"
        L"Export: C source array.\n\n"
        L"Mouse: left button toggles and draws pixels, right button erases.\n"
        L"Keyboard: arrow keys select glyphs, PgUp/PgDn previous/next,\n"
        L"type any character to jump to its glyph.\n\n"
        L"Copyright (c) 2026 Erdem Ersoy. MIT License.",
        L"About " APP_NAME, MB_OK | MB_ICONINFORMATION);
}

static void on_command(int id, int code, HWND ctl)
{
    switch (id) {
    case IDC_EDITOR:
        if (code == GEN_BEGINEDIT)
            begin_change();
        else if (code == GEN_CHANGED) {
            app.dirty = TRUE;
            InvalidateRect(app.charmap, NULL, FALSE);
            InvalidateRect(app.preview, NULL, FALSE);
            update_status();
            update_title();
        } else if (code == GEN_HOVER)
            update_status();
        return;
    case IDC_CHARMAP:
        if (code == CMN_SELCHANGE)
            select_glyph((int)SendMessageW(ctl, VFC_GETCHAR, 0, 0));
        return;
    case IDC_SAMPLE:
        if (code == EN_CHANGE)
            update_preview_text();
        return;

    case IDM_FILE_NEW:        cmd_new(); break;
    case IDM_FILE_OPEN:       cmd_open(); break;
    case IDM_FILE_SAVE:       cmd_save(); break;
    case IDM_FILE_SAVEAS:     cmd_save_as(); break;
    case IDM_FILE_EXPORT_C:   cmd_export_c(); break;
    case IDM_FILE_EXIT:       SendMessageW(app.wnd, WM_CLOSE, 0, 0); break;

    case IDM_EDIT_UNDO:       cmd_undo_redo(FALSE); break;
    case IDM_EDIT_REDO:       cmd_undo_redo(TRUE); break;
    case IDM_EDIT_CUT:        cmd_copy(); glyph_op(glyph_clear); break;
    case IDM_EDIT_COPY:       cmd_copy(); break;
    case IDM_EDIT_PASTE:      cmd_paste(); break;
    case IDM_EDIT_CLEAR:      glyph_op(glyph_clear); break;

    case IDM_GLYPH_INVERT:    glyph_op(glyph_invert); break;
    case IDM_GLYPH_FLIP_H:    glyph_op(glyph_flip_horizontal); break;
    case IDM_GLYPH_FLIP_V:    glyph_op(glyph_flip_vertical); break;
    case IDM_GLYPH_SHIFT_UP:    shift_op(0, -1); break;
    case IDM_GLYPH_SHIFT_DOWN:  shift_op(0, 1); break;
    case IDM_GLYPH_SHIFT_LEFT:  shift_op(-1, 0); break;
    case IDM_GLYPH_SHIFT_RIGHT: shift_op(1, 0); break;
    case IDM_GLYPH_BOLD:      glyph_op(glyph_bold); break;
    case IDM_GLYPH_PREV:      select_glyph(app.cur - 1); break;
    case IDM_GLYPH_NEXT:      select_glyph(app.cur + 1); break;

    case IDM_FONT_HEIGHT:     cmd_change_height(); break;
    case IDM_FONT_RENDER:     cmd_render_font(); break;

    case IDM_VIEW_GRID:       app.show_grid = !app.show_grid; set_view_options(); break;
    case IDM_VIEW_NINEDOT:    app.nine_dot = !app.nine_dot; set_view_options(); break;

    case IDM_HELP_ABOUT:      cmd_about(); break;
    }
}

static void on_init_menu(HMENU menu)
{
    EnableMenuItem(menu, IDM_EDIT_UNDO, MF_BYCOMMAND | (undo_can_undo(&app.undo) ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(menu, IDM_EDIT_REDO, MF_BYCOMMAND | (undo_can_redo(&app.undo) ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(menu, IDM_EDIT_PASTE, MF_BYCOMMAND | (can_paste() ? MF_ENABLED : MF_GRAYED));
    CheckMenuItem(menu, IDM_VIEW_GRID, MF_BYCOMMAND | (app.show_grid ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu, IDM_VIEW_NINEDOT, MF_BYCOMMAND | (app.nine_dot ? MF_CHECKED : MF_UNCHECKED));
}

/* ------------------------------------------------------------------------ */
/* Main window                                                              */

static void create_ui_font(void)
{
    NONCLIENTMETRICSW ncm;
    HDC dc;
    HGDIOBJ old;
    TEXTMETRICW tm;
    SIZE sz;

    ZeroMemory(&ncm, sizeof(ncm));
    ncm.cbSize = sizeof(ncm);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
        app.ui_font = CreateFontIndirectW(&ncm.lfMessageFont);
    if (!app.ui_font)
        app.ui_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    dc = GetDC(NULL);
    app.dpi = GetDeviceCaps(dc, LOGPIXELSY);
    old = SelectObject(dc, app.ui_font);
    GetTextMetricsW(dc, &tm);
    GetTextExtentPoint32W(dc, L"Sample text:", 12, &sz);
    SelectObject(dc, old);
    ReleaseDC(NULL, dc);
    app.ui_text_h = tm.tmHeight;
    app.label_w = sz.cx;
}

static BOOL on_create(HWND hwnd)
{
    HINSTANCE inst = app.inst;

    app.wnd = hwnd;
    app.status = CreateWindowExW(0, STATUSCLASSNAMEW, NULL,
                                 WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
                                 0, 0, 0, 0, hwnd, (HMENU)IDC_STATUS, inst, NULL);
    app.editor = CreateWindowExW(0, GLYPH_EDITOR_CLASS, NULL, WS_CHILD | WS_VISIBLE,
                                 0, 0, 0, 0, hwnd, (HMENU)IDC_EDITOR, inst, NULL);
    app.charmap = CreateWindowExW(0, CHAR_MAP_CLASS, NULL, WS_CHILD | WS_VISIBLE,
                                  0, 0, 0, 0, hwnd, (HMENU)IDC_CHARMAP, inst, NULL);
    app.sample_label = CreateWindowExW(0, L"STATIC", L"Sample text:",
                                       WS_CHILD | WS_VISIBLE | SS_LEFT,
                                       0, 0, 0, 0, hwnd, (HMENU)IDC_SAMPLE_LABEL, inst, NULL);
    app.sample = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", SAMPLE_TEXT,
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                 0, 0, 0, 0, hwnd, (HMENU)IDC_SAMPLE, inst, NULL);
    app.preview = CreateWindowExW(WS_EX_CLIENTEDGE, PREVIEW_CLASS, NULL, WS_CHILD | WS_VISIBLE,
                                  0, 0, 0, 0, hwnd, (HMENU)IDC_PREVIEW, inst, NULL);
    if (!app.status || !app.editor || !app.charmap || !app.sample || !app.preview)
        return FALSE;

    SendMessageW(app.editor, WM_SETFONT, (WPARAM)app.ui_font, FALSE);
    SendMessageW(app.sample_label, WM_SETFONT, (WPARAM)app.ui_font, FALSE);
    SendMessageW(app.sample, WM_SETFONT, (WPARAM)app.ui_font, FALSE);
    SendMessageW(app.sample, EM_LIMITTEXT, 1000, 0);

    SendMessageW(app.editor, VFC_SETFONT, 0, (LPARAM)&app.font);
    SendMessageW(app.charmap, VFC_SETFONT, 0, (LPARAM)&app.font);
    SendMessageW(app.preview, VFC_SETFONT, 0, (LPARAM)&app.font);
    set_view_options();
    update_preview_text();
    DragAcceptFiles(hwnd, TRUE);
    return TRUE;
}

static LRESULT CALLBACK main_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        return on_create(hwnd) ? 0 : -1;

    case WM_SIZE:
        layout();
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO *mmi = (MINMAXINFO *)lp;
        mmi->ptMinTrackSize.x = scale_px(640);
        mmi->ptMinTrackSize.y = scale_px(520);
        return 0;
    }

    case WM_COMMAND:
        on_command(LOWORD(wp), HIWORD(wp), (HWND)lp);
        return 0;

    case WM_INITMENUPOPUP:
        on_init_menu((HMENU)wp);
        return 0;

    case WM_KEYDOWN:
        switch (wp) {
        case VK_LEFT:  select_glyph(app.cur - 1); return 0;
        case VK_RIGHT: select_glyph(app.cur + 1); return 0;
        case VK_UP:    select_glyph(app.cur - 16); return 0;
        case VK_DOWN:  select_glyph(app.cur + 16); return 0;
        case VK_HOME:  select_glyph(0); return 0;
        case VK_END:   select_glyph(255); return 0;
        }
        break;

    case WM_CHAR:
        /* Typing a character jumps to its glyph. */
        if (wp >= 0x20) {
            int c = unicode_to_cp437((unsigned)wp);
            if (c >= 0)
                select_glyph(c);
        }
        return 0;

    case WM_MOUSEWHEEL:
        select_glyph(app.cur + (GET_WHEEL_DELTA_WPARAM(wp) > 0 ? -1 : 1));
        return 0;

    case WM_DROPFILES: {
        HDROP drop = (HDROP)wp;
        WCHAR file[MAX_PATH];

        if (DragQueryFileW(drop, 0, file, MAX_PATH) && confirm_discard())
            open_file(file);
        DragFinish(drop);
        return 0;
    }

    case WM_CLOSE:
        if (confirm_discard())
            DestroyWindow(hwnd);
        return 0;

    case WM_QUERYENDSESSION:
        return confirm_discard();

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* While the sample text box has focus, editing keys belong to it. */
static BOOL use_accelerator(const MSG *msg)
{
    if (GetFocus() != app.sample)
        return TRUE;
    if (msg->message != WM_KEYDOWN || !(GetKeyState(VK_CONTROL) & 0x8000))
        return FALSE;
    return msg->wParam == 'N' || msg->wParam == 'O' || msg->wParam == 'S';
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd_line, int show)
{
    INITCOMMONCONTROLSEX icc;
    WNDCLASSEXW wc;
    RECT work;
    MSG msg;
    LPWSTR *argv;
    int argc, w, h;

    (void)prev;
    (void)cmd_line;
    app.inst = inst;
    app.show_grid = TRUE;
    app.cur = 'A';
    font_init(&app.font, 16);
    undo_init(&app.undo, UNDO_LIMIT);

    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_BAR_CLASSES | ICC_UPDOWN_CLASS | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);
    app.clip_format = RegisterClipboardFormatW(L"VgaFontEditor.Glyph");
    create_ui_font();

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = main_proc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszMenuName = MAKEINTRESOURCEW(IDR_MAINMENU);
    wc.lpszClassName = MAIN_CLASS;
    if (!RegisterClassExW(&wc) || !glyph_editor_register(inst) ||
        !char_map_register(inst) || !preview_register(inst)) {
        MessageBoxW(NULL, L"Could not register window classes.", APP_NAME, MB_OK | MB_ICONERROR);
        return 1;
    }

    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    w = min(scale_px(1000), work.right - work.left);
    h = min(scale_px(760), work.bottom - work.top);
    if (!CreateWindowExW(WS_EX_ACCEPTFILES, MAIN_CLASS, APP_NAME,
                         WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                         CW_USEDEFAULT, CW_USEDEFAULT, w, h, NULL, NULL, inst, NULL)) {
        MessageBoxW(NULL, L"Could not create the main window.", APP_NAME, MB_OK | MB_ICONERROR);
        return 1;
    }
    app.accel = LoadAcceleratorsW(inst, MAKEINTRESOURCEW(IDR_ACCEL));

    refresh_views(TRUE);
    set_hint(L"Draw with the mouse, or use Font > Render from Windows Font to start.");
    ShowWindow(app.wnd, show);
    UpdateWindow(app.wnd);
    SetFocus(app.wnd);

    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        if (argc > 1)
            open_file(argv[1]);
        LocalFree(argv);
    }

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE && GetFocus() == app.sample) {
            SetFocus(app.wnd);
            continue;
        }
        if (use_accelerator(&msg) && TranslateAcceleratorW(app.wnd, app.accel, &msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    undo_free(&app.undo);
    if (app.ui_font)
        DeleteObject(app.ui_font);
    return (int)msg.wParam;
}
