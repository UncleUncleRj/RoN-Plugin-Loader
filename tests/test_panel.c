#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "check.h"
#include "loader/panel.h"
#include "loader/order.h"

static LoaderPlugin g_pl[ORDER_MAX];
static int g_hooks[ORDER_MAX];
static Panel g_p;

static void plugin(int i, const wchar_t *id, const char *author, const char *desc, int enabled, LpState st,
                   const char *reason)
{
    LoaderPlugin *p = &g_pl[i];
    memset(p, 0, sizeof *p);
    wcscpy(p->id, id);
    WideCharToMultiByte(CP_UTF8, 0, id, -1, p->m.name, sizeof p->m.name, NULL, NULL);
    strcpy(p->m.version, "1.0");
    strcpy(p->m.author, author);
    strcpy(p->m.description, desc);
    p->enabled = enabled;
    p->state = st;
    strcpy(p->reason, reason);
}

/* A: loaded, 2 hooks, author Ann, a 70-character description; B: off; C: failed. */
static Panel *three(void)
{
    plugin(0, L"A", "Ann", "abcd abcd abcd abcd abcd abcd abcd abcd abcd abcd abcd abcd abcd abcdefghi", 1, LP_LOADED, "");
    plugin(1, L"B", "", "", 0, LP_DISABLED, "");
    plugin(2, L"C", "", "", 1, LP_FAILED, "LoadLibrary error 193");
    g_hooks[0] = 2;
    g_hooks[1] = 0;
    g_hooks[2] = 0;
    panel_init(&g_p, g_pl, 3, g_hooks, 0);
    return &g_p;
}

TEST(panel_init_rows)
{
    Panel *p = three();
    CHECK_EQ(p->n, 3);
    CHECK_EQ(p->rows[2].failed, 1);
    CHECK_EQ(p->rows[1].enabled, 0);
    CHECK_EQ(p->rows[0].loaded, 1);
    CHECK_EQ(p->rows[0].hooks, 2);
    CHECK_EQ(p->selected, -1);
    CHECK_EQ(panel_changed(p), 0);
}

TEST(panel_toggle_twice_unchanged)
{
    Panel *p = three();
    panel_toggle(p, 1);
    CHECK_EQ(panel_changed(p), 1);
    panel_toggle(p, 1);
    CHECK_EQ(panel_changed(p), 0);
}

TEST(panel_move_follows_selection)
{
    Panel *p = three();
    panel_select(p, 2);
    CHECK_EQ(panel_move(p, -1), 1);
    CHECK(wcscmp(p->rows[1].id, L"C") == 0);
    CHECK_EQ(p->selected, 1);
    CHECK_EQ(panel_changed(p), 1);
    CHECK_EQ(panel_move(p, +1), 1);
    CHECK_EQ(panel_changed(p), 0);
}

TEST(panel_move_at_edges)
{
    Panel *p = three();
    panel_select(p, 0);
    CHECK_EQ(panel_move(p, -1), 0);
    panel_select(p, 2);
    CHECK_EQ(panel_move(p, +1), 0);
    panel_select(p, -1);
    CHECK_EQ(panel_move(p, -1), 0);
    CHECK_EQ(panel_changed(p), 0);
}

TEST(panel_description_lines)
{
    Panel *p = three();
    PanelLine l[PANEL_DESC_LINES];
    panel_select(p, 0);
    CHECK_EQ(panel_description(p, l, PANEL_WRAP), 6);
    CHECK_STR(l[0].text, "A");
    CHECK_EQ(l[0].tone, PANEL_TONE_TEXT);
    CHECK_STR(l[1].text, "Version 1.0, by Ann");
    CHECK_EQ(l[1].tone, PANEL_TONE_DIM);
    CHECK(strlen(l[2].text) <= PANEL_WRAP);
    CHECK(strstr(l[3].text, "abcdefghi") != NULL);
    CHECK(strstr(l[3].text, "...") == NULL);
    CHECK_STR(l[4].text, "File: A.dll");
    CHECK_EQ(l[4].tone, PANEL_TONE_DIM);
    CHECK_STR(l[5].text, "Hooks 2 game functions.");
}

TEST(panel_description_wraps_utf8)
{
    Panel *p = three();
    char desc[256] = "";
    for (int i = 0; i < 130 && strlen(desc) + 2 < sizeof desc; i++)
        strcat(desc, "\xC3\xA9");  /* U+00E9, two bytes */
    strcpy(p->rows[0].description, desc);
    panel_select(p, 0);
    PanelLine l[PANEL_DESC_LINES];
    CHECK_EQ(panel_description(p, l, PANEL_WRAP), 6);
    for (int i = 2; i <= 3; i++) {
        wchar_t w[128];
        CHECK(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, l[i].text, -1, w, 128) > 0);
    }
    size_t l3 = strlen(l[3].text);
    CHECK(l3 >= 3 && strcmp(l[3].text + l3 - 3, "...") == 0);
}

TEST(panel_description_status)
{
    Panel *p = three();
    PanelLine l[PANEL_DESC_LINES];
    panel_select(p, 1);
    int n = panel_description(p, l, PANEL_WRAP);
    CHECK_EQ(n, 4);
    CHECK_STR(l[1].text, "Version 1.0");
    CHECK_STR(l[2].text, "File: B.dll");
    CHECK_STR(l[3].text, "Turned off.");
    CHECK_EQ(l[3].tone, PANEL_TONE_DIM);
    panel_select(p, 2);
    n = panel_description(p, l, PANEL_WRAP);
    CHECK_STR(l[n - 1].text, "Failed: LoadLibrary error 193");
    CHECK_EQ(l[n - 1].tone, PANEL_TONE_BAD);
    panel_select(p, 0);
    p->rows[0].hooks = 1;
    n = panel_description(p, l, PANEL_WRAP);
    CHECK_STR(l[n - 1].text, "Hooks 1 game function.");
    p->rows[0].hooks = 0;
    n = panel_description(p, l, PANEL_WRAP);
    CHECK_STR(l[n - 1].text, "Hooks no game functions.");
    p->safe_start = 1;
    p->rows[0].loaded = 0;
    n = panel_description(p, l, PANEL_WRAP);
    CHECK_STR(l[n - 1].text, "Not loaded in this start.");
}

TEST(panel_description_without_selection)
{
    Panel *p = three();
    PanelLine l[PANEL_DESC_LINES];
    panel_select(p, -1);
    CHECK_EQ(panel_description(p, l, PANEL_WRAP), 1);
    CHECK_STR(l[0].text, "Click a plugin to see its description.");
    CHECK_EQ(l[0].tone, PANEL_TONE_DIM);
    panel_init(&g_p, g_pl, 0, g_hooks, 0);
    CHECK_EQ(panel_description(&g_p, l, PANEL_WRAP), 0);
}

TEST(panel_description_file_name_utf8)
{
    plugin(0, L"\x00DC" L"ber", "", "", 1, LP_LOADED, "");  /* "Über" */
    g_hooks[0] = 0;
    panel_init(&g_p, g_pl, 1, g_hooks, 0);
    panel_select(&g_p, 0);
    PanelLine l[PANEL_DESC_LINES];
    int n = panel_description(&g_p, l, PANEL_WRAP);
    CHECK(n >= 3);
    CHECK_STR(l[n - 2].text, "File: \xC3\x9C" "ber.dll");
}

TEST(panel_hit_rows)
{
    Panel *p = three();
    int box = -1;
    CHECK_EQ(panel_hit(p, 328, 70, 36 + 5, &box), 0);
    CHECK_EQ(box, 0);
    CHECK_EQ(panel_hit(p, 328, 30, 36 + 24 + 5, &box), 1);
    CHECK_EQ(box, 1);
    CHECK_EQ(panel_hit(p, 328, 70, 10, &box), -1);
    CHECK_EQ(panel_hit(p, 328, 70, 36 + 3 * 24 + 5, &box), -1);  /* below the last row */
}

TEST(panel_selected_row_keeps_its_height)
{
    Panel *p = three();
    panel_select(p, 0);
    PanelSlot s[ORDER_MAX];
    CHECK_EQ(panel_layout(p, 328, s, ORDER_MAX), 3);
    CHECK_EQ(s[0].h, 24);
    CHECK_EQ(s[1].y, 36 + 24);
    int box;
    CHECK_EQ(panel_hit(p, 328, 70, 36 + 24 + 5, &box), 1);
}

TEST(panel_hit_scrolled)
{
    for (int i = 0; i < 20; i++) {
        wchar_t id[16];
        swprintf(id, 16, L"P%02d", i);
        plugin(i, id, "", "", 1, LP_LOADED, "");
        g_hooks[i] = 0;
    }
    panel_init(&g_p, g_pl, 20, g_hooks, 0);
    panel_scroll(&g_p, 328, -5);
    CHECK_EQ(g_p.scroll, 5);
    int box;
    CHECK_EQ(panel_hit(&g_p, 328, 70, 36 + 5, &box), 5);
    panel_scroll(&g_p, 328, -100);
    CHECK_EQ(g_p.scroll, 20 - (328 - 36) / 24);
    panel_scroll(&g_p, 328, +100);
    CHECK_EQ(g_p.scroll, 0);
}

TEST(panel_notice_order)
{
    Panel *p = three();
    CHECK(panel_notice(p) == NULL);
    panel_toggle(p, 0);
    CHECK_STR(panel_notice(p), "Not applied yet. Apply and Restart to use this list.");
    p->safe_start = 1;
    CHECK_STR(panel_notice(p), "The last start did not reach the menu, so this start runs without plugins. "
                               "Turn off the plugin that stopped it, then Apply and Restart.");
    p->save_failed = 1;
    CHECK_STR(panel_notice(p), "Could not save plugins\\load-order.txt. Nothing was changed.");
}

static void temp_file(wchar_t *path, const wchar_t *name)
{
    GetTempPathW(MAX_PATH, path);
    wchar_t tail[64];
    swprintf(tail, 64, L"ron_panel_%lu_%ls", GetCurrentProcessId(), name);
    wcscat(path, tail);
}

TEST(panel_save_writes_order)
{
    Panel *p = three();
    panel_select(p, 2);
    panel_move(p, -1);
    panel_move(p, -1);  /* C, A, B */
    panel_toggle(p, 2); /* B was off: now on */
    panel_toggle(p, 2); /* and off again */
    wchar_t path[MAX_PATH];
    temp_file(path, L"order.txt");
    CHECK_EQ(panel_save(p, path), 1);
    CHECK_EQ(p->save_failed, 0);
    OrderItem items[3];
    memset(items, 0, sizeof items);
    wcscpy(items[0].name, L"C");
    items[0].enabled = 1;
    wcscpy(items[1].name, L"A");
    items[1].enabled = 1;
    wcscpy(items[2].name, L"B");
    items[2].enabled = 0;
    char want[1024], got[1024];
    size_t wl = order_serialize(items, 3, want, sizeof want);
    FILE *f = _wfopen(path, L"rb");
    CHECK(f != NULL);
    size_t gl = f ? fread(got, 1, sizeof got - 1, f) : 0;
    if (f)
        fclose(f);
    got[gl] = 0;
    CHECK_EQ(gl, wl);
    CHECK_STR(got, want);
    DeleteFileW(path);
}

TEST(panel_save_fails_cleanly)
{
    Panel *p = three();
    wchar_t path[MAX_PATH];
    temp_file(path, L"dir");
    CreateDirectoryW(path, NULL);
    CHECK_EQ(panel_save(p, path), 0);
    CHECK_EQ(p->save_failed, 1);
    CHECK_STR(panel_notice(p), "Could not save plugins\\load-order.txt. Nothing was changed.");
    RemoveDirectoryW(path);
}

TEST(panel_empty)
{
    panel_init(&g_p, g_pl, 0, g_hooks, 0);
    int box;
    PanelSlot s[4];
    CHECK_EQ(panel_hit(&g_p, 328, 70, 40, &box), -1);
    CHECK_EQ(panel_layout(&g_p, 328, s, 4), 0);
    CHECK(panel_notice(&g_p) == NULL);
}

/* Final review: with more plugins than rows fit (156 px: 5 rows), Move Up/Down keep the selected row
   in view, and the list can say how many rows are hidden above and below. */
static void twenty(void)
{
    for (int i = 0; i < 20; i++) {
        wchar_t id[16];
        swprintf(id, 16, L"P%02d", i);
        plugin(i, id, "", "", 1, LP_LOADED, "");
        g_hooks[i] = 0;
    }
    panel_init(&g_p, g_pl, 20, g_hooks, 0);
}

TEST(panel_move_keeps_selection_visible)
{
    twenty();
    panel_select(&g_p, 4);
    CHECK_EQ(panel_move(&g_p, +1), 1);
    panel_keep_visible(&g_p, 156);
    CHECK_EQ(g_p.selected, 5);
    CHECK_EQ(g_p.scroll, 1);
    panel_select(&g_p, 1);
    CHECK_EQ(panel_move(&g_p, -1), 1);
    panel_keep_visible(&g_p, 156);
    CHECK_EQ(g_p.scroll, 0);
    panel_select(&g_p, -1);
    g_p.scroll = 3;
    panel_keep_visible(&g_p, 156);
    CHECK_EQ(g_p.scroll, 3);
}

TEST(panel_more_counts)
{
    twenty();
    int above = -1, below = -1;
    panel_more(&g_p, 156, &above, &below);
    CHECK_EQ(above, 0);
    CHECK_EQ(below, 15);
    panel_scroll(&g_p, 156, -5);
    panel_more(&g_p, 156, &above, &below);
    CHECK_EQ(above, 5);
    CHECK_EQ(below, 10);
    Panel *p = three();
    panel_more(p, 156, &above, &below);
    CHECK_EQ(above, 0);
    CHECK_EQ(below, 0);
}

/* A narrow Description box (a mod's narrower menu) wraps at its own width. */
TEST(panel_description_wraps_to_width)
{
    Panel *p = three();
    panel_select(p, 0);
    PanelLine l[PANEL_DESC_LINES];
    CHECK_EQ(panel_description(p, l, 30), 6);
    CHECK(strlen(l[2].text) <= 30);
    CHECK(strlen(l[3].text) <= 30);
    size_t n = strlen(l[3].text);
    CHECK(n >= 3 && strcmp(l[3].text + n - 3, "...") == 0);
}
