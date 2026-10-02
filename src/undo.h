/*
 * Undo/redo history made of whole-font snapshots (a font is only ~8 KB).
 */
#ifndef UNDO_H
#define UNDO_H

#include "font.h"

typedef struct UndoEntry {
    VgaFont font;
    int ch;  /* glyph that was selected, so undo can show the change */
} UndoEntry;

typedef struct UndoList {
    UndoEntry *items;
    int count, cap;
} UndoList;

typedef struct UndoHistory {
    UndoList undo, redo;
    int limit;
} UndoHistory;

void undo_init(UndoHistory *h, int limit);
void undo_free(UndoHistory *h);
void undo_clear(UndoHistory *h);

/* Records the state before a change. Clears the redo list. */
int  undo_push(UndoHistory *h, const VgaFont *font, int ch);

/* Restore the previous/next state into font and *ch. Return 0 if empty. */
int  undo_undo(UndoHistory *h, VgaFont *font, int *ch);
int  undo_redo(UndoHistory *h, VgaFont *font, int *ch);

int  undo_can_undo(const UndoHistory *h);
int  undo_can_redo(const UndoHistory *h);

#endif
