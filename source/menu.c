#include "menu.h"

#include <stdio.h>
#include <string.h>

void menuOpen(Menu *m, const char *title)
{
    memset(m, 0, sizeof(*m));
    snprintf(m->title, sizeof(m->title), "%s", title);
    m->open = true;
}

void menuClose(Menu *m)
{
    m->open = false;
}

bool menuAdd(Menu *m, const char *label, int action, int arg)
{
    if (m->count >= MENU_MAX_ITEMS)
        return false;
    MenuItem *item = &m->items[m->count++];
    snprintf(item->label, sizeof(item->label), "%s", label);
    item->detail[0] = '\0';
    item->checked = false;
    item->action = action;
    item->arg = arg;
    return true;
}

void menuMarkLast(Menu *m, const char *detail, bool checked)
{
    if (m->count == 0)
        return;
    MenuItem *item = &m->items[m->count - 1];
    snprintf(item->detail, sizeof(item->detail), "%s", detail ? detail : "");
    item->checked = checked;
}

void menuAddDivider(Menu *m)
{
    m->divider = m->count;
}

void menuMove(Menu *m, int delta)
{
    if (m->count == 0)
        return;
    int next = m->cursor + delta;
    if (next < 0)
        next = 0;
    if (next >= m->count)
        next = m->count - 1;
    m->cursor = next;
}

const MenuItem *menuSelected(const Menu *m)
{
    return m->open && m->count > 0 ? &m->items[m->cursor] : NULL;
}
