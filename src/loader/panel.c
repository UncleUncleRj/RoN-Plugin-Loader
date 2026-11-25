#include <stdio.h>
#include <string.h>
#include "loader/panel.h"
#include "loader/order.h"

void panel_init(Panel *p, const LoaderPlugin *plugins, int n, const int *hooks, int safe_start)
{
    memset(p, 0, sizeof *p);
    if (n < 0)
        n = 0;
    if (n > ORDER_MAX)
        n = ORDER_MAX;
    p->n = n;
    p->selected = -1;
    p->safe_start = safe_start;
    for (int i = 0; i < n; i++) {
        const LoaderPlugin *lp = &plugins[i];
        PanelRow *r = &p->rows[i];
        memcpy(r->id, lp->id, sizeof r->id);
        snprintf(r->name, sizeof r->name, "%s", lp->m.name);
        snprintf(r->version, sizeof r->version, "%s", lp->m.version);
        snprintf(r->author, sizeof r->author, "%s", lp->m.author);
        snprintf(r->description, sizeof r->description, "%s", lp->m.description);
        snprintf(r->reason, sizeof r->reason, "%s", lp->reason);
        r->enabled = lp->enabled;
        r->is_new = lp->is_new;
        r->failed = lp->enabled && lp->reason[0];
        r->loaded = lp->state == LP_LOADED;
        r->hooks = hooks ? hooks[i] : 0;
        memcpy(p->start_ids[i], lp->id, sizeof p->start_ids[i]);
        p->start_enabled[i] = lp->enabled;
    }
}

void panel_select(Panel *p, int row) { p->selected = row >= 0 && row < p->n ? row : -1; }

void panel_toggle(Panel *p, int row)
{
    if (row >= 0 && row < p->n)
        p->rows[row].enabled = !p->rows[row].enabled;
}

int panel_move(Panel *p, int delta)
{
    int from = p->selected, to = from + delta;
    if (from < 0 || from >= p->n || to < 0 || to >= p->n || to == from)
        return 0;
    PanelRow t = p->rows[from];
    p->rows[from] = p->rows[to];
    p->rows[to] = t;
    p->selected = to;
    return 1;
}

int panel_changed(const Panel *p)
{
    for (int i = 0; i < p->n; i++)
        if (wcscmp(p->rows[i].id, p->start_ids[i]) != 0 || !p->rows[i].enabled != !p->start_enabled[i])
            return 1;
    return 0;
}

static size_t char_len(unsigned char c)
{
    if (c < 0x80)
        return 1;
    if ((c >> 5) == 6)
        return 2;
    if ((c >> 4) == 14)
        return 3;
    if ((c >> 3) == 30)
        return 4;
    return 1;
}

/* Bytes of the first `chars` characters of s, never more than max_bytes, never inside a character. */
static size_t take(const char *s, int chars, size_t max_bytes)
{
    size_t b = 0;
    for (int c = 0; c < chars && s[b]; c++) {
        size_t l = char_len((unsigned char)s[b]);
        if (b + l > max_bytes)
            break;
        b += l;
    }
    return b;
}

static int chars_of(const char *s)
{
    int c = 0;
    for (size_t b = 0; s[b]; b += char_len((unsigned char)s[b]))
        c++;
    return c;
}

/* The description in at most 2 lines of PANEL_WRAP characters, "..." when it is cut. */
static int wrap(const char *s, char lines[][128])
{
    while (*s == ' ')
        s++;
    if (!*s)
        return 0;
    if (chars_of(s) <= PANEL_WRAP && strlen(s) < 128) {
        snprintf(lines[0], 128, "%s", s);
        return 1;
    }
    size_t cut = take(s, PANEL_WRAP, 124);
    size_t end = cut;
    while (end > 0 && s[end] != ' ')
        end--;
    if (end == 0)
        end = cut;  /* no space: a hard break */
    memcpy(lines[0], s, end);
    lines[0][end] = 0;
    s += end;
    while (*s == ' ')
        s++;
    if (chars_of(s) <= PANEL_WRAP && strlen(s) < 128) {
        snprintf(lines[1], 128, "%s", s);
    } else {
        size_t b = take(s, PANEL_WRAP - 3, 121);
        memcpy(lines[1], s, b);
        memcpy(lines[1] + b, "...", 4);
    }
    return 2;
}

int panel_details(const Panel *p, int row, char lines[4][128])
{
    if (row < 0 || row >= p->n)
        return 0;
    const PanelRow *r = &p->rows[row];
    int n = 0;
    if (r->author[0])
        snprintf(lines[n++], 128, "by %s", r->author);
    n += wrap(r->description, lines + n);
    if (r->failed)
        snprintf(lines[n++], 128, "Failed: %s", r->reason);
    else if (!r->enabled)
        snprintf(lines[n++], 128, "Turned off.");
    else if (p->safe_start && !r->loaded)
        snprintf(lines[n++], 128, "Not loaded in this start.");
    else if (r->hooks == 0)
        snprintf(lines[n++], 128, "Hooks no game functions.");
    else if (r->hooks == 1)
        snprintf(lines[n++], 128, "Hooks 1 game function.");
    else
        snprintf(lines[n++], 128, "Hooks %d game functions.", r->hooks);
    return n;
}

int panel_layout(const Panel *p, int list_h, PanelSlot *out, int max)
{
    int k = 0, y = PANEL_INTRO_H;
    for (int i = p->scroll; i < p->n && k < max; i++) {
        if (y + PANEL_ROW_H > list_h)
            break;
        int h = PANEL_ROW_H;
        if (i == p->selected) {
            char l[4][128];
            h += panel_details(p, i, l) * PANEL_LINE_H + 4;
        }
        out[k].row = i;
        out[k].y = y;
        out[k].h = h;
        k++;
        y += h;
    }
    return k;
}

int panel_hit(const Panel *p, int list_h, int x, int y, int *on_box)
{
    PanelSlot s[ORDER_MAX];
    int n = panel_layout(p, list_h, s, ORDER_MAX);
    if (on_box)
        *on_box = 0;
    for (int i = 0; i < n; i++) {
        if (y < s[i].y || y >= s[i].y + s[i].h)
            continue;
        if (on_box)
            *on_box = x >= PANEL_BOX_X && x < PANEL_BOX_X + PANEL_BOX_W && y < s[i].y + PANEL_ROW_H;
        return s[i].row;
    }
    return -1;
}

void panel_scroll(Panel *p, int list_h, int notches)
{
    int visible = (list_h - PANEL_INTRO_H) / PANEL_ROW_H;
    int max = p->n - (visible > 0 ? visible : 0);
    if (max < 0)
        max = 0;
    int s = p->scroll - notches;
    p->scroll = s < 0 ? 0 : s > max ? max : s;
}

const char *panel_notice(const Panel *p)
{
    if (p->save_failed)
        return "Could not save plugins\\load-order.txt. Nothing was changed.";
    if (p->safe_start)
        return "The last start did not reach the menu, so this start runs without plugins. "
               "Turn off the plugin that stopped it, then Apply and Restart.";
    if (panel_changed(p))
        return "Not applied yet. Apply and Restart to use this list.";
    return NULL;
}

int panel_save(Panel *p, const wchar_t *path)
{
    static OrderItem items[ORDER_MAX];
    static char text[ORDER_MAX * 200 + 256];
    for (int i = 0; i < p->n; i++) {
        memset(&items[i], 0, sizeof items[i]);
        memcpy(items[i].name, p->rows[i].id, sizeof items[i].name);
        items[i].enabled = p->rows[i].enabled;
    }
    size_t len = order_serialize(items, p->n, text, sizeof text);
    FILE *f = len ? _wfopen(path, L"wb") : NULL;
    int ok = f && fwrite(text, 1, len, f) == len;
    if (f && fclose(f) != 0)
        ok = 0;
    p->save_failed = !ok;
    return ok;
}
