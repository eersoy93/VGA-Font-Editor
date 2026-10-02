#ifndef DIALOGS_H
#define DIALOGS_H

#include <windows.h>

#include "font.h"

/* Asks for the glyph height of a new font. Returns 0 if cancelled. */
int dialog_new_font(HWND owner, int default_height);

/* Asks for a new glyph height and resize method. Returns FALSE if cancelled. */
BOOL dialog_resize_font(HWND owner, int current_height, int *new_height,
                        FontResizeMode *mode);

#endif
