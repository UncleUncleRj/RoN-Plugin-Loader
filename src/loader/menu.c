/*
 * The Plugins button and panel in the main menu (design section 8; plan
 * docs/plans/2026-11-24-plugins-panel.md, task 5). Six owner-0 hooks, installed after every plugin:
 *  - GetTopMenuXMLFile: serves a copy of the menu file with the Plugins button and panel added;
 *  - TopMenu::check_result: "plugins" opens the panel, "plugins_apply" saves and restarts;
 *  - TopMenuSelection's virtual table: drawing, left click and wheel (menu_draw.c) and button clicks
 *    (Move Up / Move Down without the panel closing).
 * Engine facts: decomp/ron-ee-6674863f/notes/2026-11-24-plugins-panel-menu.txt (root repo).
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "loader/menu.h"
#include "loader/menu_draw.h"
#include "loader/game.h"
#include "loader/hooks.h"
#include "loader/loader.h"
#include "loader/log.h"
#include "loader/menufile.h"
#include "loader/panel.h"

#define THISCALL __attribute__((thiscall))

Panel g_panel;
static wchar_t g_cache_path[MAX_PATH];
static int g_cache_state;  /* 0 not tried yet, 1 served, -1 the game's own file */
static int g_answer;       /* check_result is hooked, so a Plugins button would be answered */

typedef void *(THISCALL *MenuFileFn)(void *out);
typedef int(THISCALL *CheckResultFn)(void *topmenu, const void *name);
typedef void(THISCALL *SelButtonFn)(void *sel, const void *name);
static MenuFileFn o_menu_file;
static CheckResultFn o_check_result;
static SelButtonFn o_sel_button;

static void plugins_dir(wchar_t *out)
{
    const RonGameInfo *g = loader_game();
    _snwprintf(out, MAX_PATH, L"%ls\\plugins", g->game_dir ? g->game_dir : L".");
    out[MAX_PATH - 1] = 0;
}

static char *read_all(const wchar_t *path, size_t *len)
{
    FILE *f = _wfopen(path, L"rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = n > 0 && n < 4 * 1024 * 1024 ? malloc((size_t)n + 1) : NULL;
    if (buf) {
        *len = fread(buf, 1, (size_t)n, f);
        buf[*len] = 0;
    }
    fclose(f);
    return buf;
}

/* The game's menu file with the button and panel added, written to plugins\.cache\topmenu.xml. */
static int make_cache(void)
{
    GString tmp;
    o_menu_file(&tmp);  /* the name the rest of the chain wants */
    wchar_t name[MAX_PATH], src[MAX_PATH], full[MAX_PATH];
    int have = gs_text(&tmp, name, MAX_PATH);
    gs_close(&tmp);
    if (!have) {
        log_line("loader", "menu: no Plugins button (no menu file name)");
        return 0;
    }
    if (!wcspbrk(name, L"\\/")) {
        if (!game_data_path(name, src, MAX_PATH))
            _snwprintf(src, MAX_PATH, L"data\\%ls", name);
    } else {
        wcsncpy(src, name, MAX_PATH);
    }
    src[MAX_PATH - 1] = 0;
    if (src[0] == L'\\' || (src[0] && src[1] == L':'))
        wcsncpy(full, src, MAX_PATH);
    else
        _snwprintf(full, MAX_PATH, L"%ls\\%ls", loader_game()->game_dir, src);
    full[MAX_PATH - 1] = 0;

    size_t len = 0;
    char *xml = read_all(full, &len);
    if (!xml) {
        log_line("loader", "menu: no Plugins button (cannot read the menu file)");
        return 0;
    }
    size_t cap = len + 8192;
    char *out = malloc(cap);
    MfWhere where = MF_FAILED;
    size_t n = out ? menufile_add_plugins(xml, len, out, cap, &where) : 0;
    free(xml);
    if (!n) {
        free(out);
        log_line("loader", "menu: no Plugins button (no Tools & Extras panel with a workshop or back button)");
        return 0;
    }
    wchar_t dir[MAX_PATH];
    plugins_dir(dir);
    wcsncat(dir, L"\\.cache", MAX_PATH - wcslen(dir) - 1);
    CreateDirectoryW(dir, NULL);
    _snwprintf(g_cache_path, MAX_PATH, L"%ls\\topmenu.xml", dir);
    g_cache_path[MAX_PATH - 1] = 0;
    FILE *f = _wfopen(g_cache_path, L"wb");
    int ok = f && fwrite(out, 1, n, f) == n;
    if (f && fclose(f) != 0)
        ok = 0;
    free(out);
    if (!ok) {
        log_line("loader", "menu: no Plugins button (cannot write plugins\\.cache\\topmenu.xml)");
        return 0;
    }
    log_line("loader", "menu: Plugins button %s",
             where == MF_AFTER_WORKSHOP ? "after Steam Workshop" : where == MF_ABOVE_BACK ? "above Back" : "already there");
    return 1;
}

/* GetTopMenuXMLFile: ECX = the String to fill, no stack arguments, plain ret (ledger menu F1). */
static void *THISCALL d_menu_file(void *out)
{
    if (g_cache_state == 0)
        g_cache_state = g_answer && make_cache() ? 1 : -1;
    if (g_cache_state == 1) {
        gs_from_static((GString *)out, g_cache_path);
        return out;
    }
    return o_menu_file(out);
}

static void open_panel(void)
{
    static int hooks[ORDER_MAX];
    int n = 0;
    const LoaderPlugin *pl = loader_plugins(&n);
    for (int i = 0; i < n; i++)
        hooks[i] = hm_owner_count(i + 1);
    panel_init(&g_panel, pl, n, hooks, loader_safe_start());
}

/* TopMenu::check_result(String const &button): thiscall, ret 4; -1 stays in the panel (ledger menu F4). */
static int THISCALL d_check_result(void *topmenu, const void *name)
{
    if (gs_is(name, "plugins")) {
        open_panel();
        return game_sub_menu(topmenu, "PLUGINS");
    }
    if (gs_is(name, "plugins_apply")) {
        wchar_t path[MAX_PATH];
        plugins_dir(path);
        wcsncat(path, L"\\load-order.txt", MAX_PATH - wcslen(path) - 1);
        if (panel_save(&g_panel, path)) {
            log_line("loader", "panel: applied, restarting");
            game_restart();
        } else {
            log_line("loader", "panel: could not save load-order.txt");
        }
        return -1;
    }
    return o_check_result(topmenu, name);
}

int menu_is_plugins_panel(void *sel) { return sel && gs_is((char *)sel + 0x4C0, "PLUGINS"); }

/* TopMenuSelection::on_button_clicked(String const &): thiscall, ret 4. Calling the original ends
   the panel (release_modal); not calling it keeps the panel open (ledger menu F4). */
static void THISCALL d_sel_button(void *sel, const void *name)
{
    if (menu_is_plugins_panel(sel)) {
        int up = gs_is(name, "plugins_up"), down = gs_is(name, "plugins_down");
        if (up || down) {
            panel_move(&g_panel, up ? -1 : 1);
            const short *L = game_find_rect(sel, "plugin_list");
            if (L)
                panel_keep_visible(&g_panel, L[3] - L[2]);
            game_click();
            return;
        }
        if (gs_is(name, "plugins_apply") && !panel_changed(&g_panel) && !g_panel.safe_start) {
            game_click();
            return;
        }
    }
    o_sel_button(sel, name);
}

int menu_install(uintptr_t delta, int *frame_hook)
{
    *frame_hook = 0;
    static const uint8_t cr_sig[9] = {0x55, 0x8B, 0xEC, 0x64, 0xA1, 0, 0, 0, 0};
    static const uint8_t file_sig[10] = {0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0, 0, 0, 0};
    static const uint8_t file_mask[10] = {1, 1, 1, 1, 1, 1, 0, 0, 0, 0};
    const uintptr_t vt = 0xB3F3F4 + delta;  /* TopMenuSelection's virtual table */
    game_init(delta);
    int ok = 0;
    g_answer = hm_inline(0, 0x59B1E0 + delta, cr_sig, NULL, 9, 9, (void *)d_check_result, (void **)&o_check_result) == RON_OK;
    ok += g_answer;
    ok += hm_ptr(0, vt + 0x2AC, 0x59E930 + delta, (void *)d_sel_button, (void **)&o_sel_button) == RON_OK;
    ok += menu_draw_install(vt, delta, frame_hook);
    if (g_answer)  /* no button the game could not answer */
        ok += hm_inline(0, 0x59F7C0 + delta, file_sig, file_mask, 10, 5, (void *)d_menu_file, (void **)&o_menu_file) == RON_OK;
    return ok;
}
