#include "dialogs.h"
#include "resource.h"

#include <commctrl.h>

typedef struct HeightDialog {
    int current_height;
    int height;
    FontResizeMode mode;
} HeightDialog;

static void init_height_spin(HWND dlg, int value)
{
    SendDlgItemMessageW(dlg, IDC_HEIGHT_SPIN, UDM_SETRANGE32, FONT_MIN_HEIGHT, FONT_MAX_HEIGHT);
    SendDlgItemMessageW(dlg, IDC_HEIGHT_SPIN, UDM_SETPOS32, 0, value);
    SendDlgItemMessageW(dlg, IDC_HEIGHT, EM_SETSEL, 0, -1);
}

/* Reads and validates the height field; shows an error and returns 0 if invalid. */
static int read_height(HWND dlg)
{
    BOOL ok;
    UINT h = GetDlgItemInt(dlg, IDC_HEIGHT, &ok, FALSE);

    if (!ok || h < FONT_MIN_HEIGHT || h > FONT_MAX_HEIGHT) {
        MessageBoxW(dlg, L"Please enter a glyph height between 1 and 32.",
                    L"VGA Font Editor", MB_OK | MB_ICONWARNING);
        SetFocus(GetDlgItem(dlg, IDC_HEIGHT));
        SendDlgItemMessageW(dlg, IDC_HEIGHT, EM_SETSEL, 0, -1);
        return 0;
    }
    return (int)h;
}

static INT_PTR CALLBACK new_font_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    HeightDialog *hd = (HeightDialog *)GetWindowLongPtrW(dlg, DWLP_USER);

    switch (msg) {
    case WM_INITDIALOG:
        hd = (HeightDialog *)lp;
        SetWindowLongPtrW(dlg, DWLP_USER, (LONG_PTR)hd);
        init_height_spin(dlg, hd->height);
        return TRUE;

    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            int h = read_height(dlg);
            if (h) {
                hd->height = h;
                EndDialog(dlg, IDOK);
            }
            return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) {
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

int dialog_new_font(HWND owner, int default_height)
{
    HeightDialog hd;

    hd.current_height = default_height;
    hd.height = default_height;
    hd.mode = FONT_RESIZE_TOP;
    if (DialogBoxParamW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_NEWFONT), owner,
                        new_font_proc, (LPARAM)&hd) != IDOK)
        return 0;
    return hd.height;
}

static INT_PTR CALLBACK resize_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    static const WCHAR *const modes[] = {
        L"Keep top rows (add/remove at bottom)",
        L"Center (add/remove at top and bottom)",
        L"Keep bottom rows (add/remove at top)",
        L"Scale glyphs (nearest neighbor)"
    };
    HeightDialog *hd = (HeightDialog *)GetWindowLongPtrW(dlg, DWLP_USER);
    int i;

    switch (msg) {
    case WM_INITDIALOG:
        hd = (HeightDialog *)lp;
        SetWindowLongPtrW(dlg, DWLP_USER, (LONG_PTR)hd);
        SetDlgItemInt(dlg, IDC_CUR_HEIGHT, (UINT)hd->current_height, FALSE);
        init_height_spin(dlg, hd->height);
        for (i = 0; i < (int)(sizeof(modes) / sizeof(modes[0])); i++)
            SendDlgItemMessageW(dlg, IDC_MODE, CB_ADDSTRING, 0, (LPARAM)modes[i]);
        SendDlgItemMessageW(dlg, IDC_MODE, CB_SETCURSEL, (WPARAM)hd->mode, 0);
        return TRUE;

    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            int h = read_height(dlg);
            if (h) {
                LRESULT sel = SendDlgItemMessageW(dlg, IDC_MODE, CB_GETCURSEL, 0, 0);
                hd->height = h;
                hd->mode = sel == CB_ERR ? FONT_RESIZE_TOP : (FontResizeMode)sel;
                EndDialog(dlg, IDOK);
            }
            return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) {
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

BOOL dialog_resize_font(HWND owner, int current_height, int *new_height,
                        FontResizeMode *mode)
{
    HeightDialog hd;

    hd.current_height = current_height;
    hd.height = *new_height;
    hd.mode = *mode;
    if (DialogBoxParamW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_RESIZE), owner,
                        resize_proc, (LPARAM)&hd) != IDOK)
        return FALSE;
    *new_height = hd.height;
    *mode = hd.mode;
    return TRUE;
}
