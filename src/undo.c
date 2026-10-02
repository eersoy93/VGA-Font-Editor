#include "undo.h"

#include <stdlib.h>
#include <string.h>

static int list_push(UndoList *list, int limit, const VgaFont *font, int ch)
{
    if (list->count == limit) {
        /* Drop the oldest entry. */
        memmove(list->items, list->items + 1,
                (size_t)(list->count - 1) * sizeof(UndoEntry));
        list->count--;
    }
    if (list->count == list->cap) {
        int new_cap = list->cap ? list->cap * 2 : 16;
        UndoEntry *p;

        if (new_cap > limit)
            new_cap = limit;
        p = (UndoEntry *)realloc(list->items, (size_t)new_cap * sizeof(UndoEntry));
        if (!p)
            return 0;
        list->items = p;
        list->cap = new_cap;
    }
    list->items[list->count].font = *font;
    list->items[list->count].ch = ch;
    list->count++;
    return 1;
}

void undo_init(UndoHistory *h, int limit)
{
    memset(h, 0, sizeof(*h));
    h->limit = limit > 0 ? limit : 1;
}

void undo_free(UndoHistory *h)
{
    free(h->undo.items);
    free(h->redo.items);
    memset(&h->undo, 0, sizeof(h->undo));
    memset(&h->redo, 0, sizeof(h->redo));
}

void undo_clear(UndoHistory *h)
{
    h->undo.count = 0;
    h->redo.count = 0;
}

int undo_push(UndoHistory *h, const VgaFont *font, int ch)
{
    h->redo.count = 0;
    return list_push(&h->undo, h->limit, font, ch);
}

/* Moves the top of 'from' into font, saving the current font onto 'to'. */
static int step(UndoHistory *h, UndoList *from, UndoList *to, VgaFont *font, int *ch)
{
    UndoEntry *top;

    if (from->count == 0)
        return 0;
    if (!list_push(to, h->limit, font, *ch))
        return 0;
    top = &from->items[--from->count];
    *font = top->font;
    *ch = top->ch;
    return 1;
}

int undo_undo(UndoHistory *h, VgaFont *font, int *ch)
{
    return step(h, &h->undo, &h->redo, font, ch);
}

int undo_redo(UndoHistory *h, VgaFont *font, int *ch)
{
    return step(h, &h->redo, &h->undo, font, ch);
}

int undo_can_undo(const UndoHistory *h)
{
    return h->undo.count > 0;
}

int undo_can_redo(const UndoHistory *h)
{
    return h->redo.count > 0;
}
