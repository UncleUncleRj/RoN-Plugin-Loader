#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "loader/loader.h"
#include "loader/log.h"
#include "loader/hooks.h"
#include "loader/events.h"
#include "loader/symbols.h"
#include "loader/safestart.h"
#include "loader/menu.h"
#include "loader/files.h"
#include "loader/files_win32.h"
#include "loader/files_game.h"
#include "common/identity.h"
#include "common/version.h"

static int g_safe;  /* this start loads no plugins: the last one never reached the menu */
static SsClear g_clear = SS_CLEAR_AT_POST_INIT;  /* when the marker goes (final review: not before the
                                                    menu has drawn, plugin callbacks and hooks included) */
static int g_cleared;

static void clear_marker(void)
{
    if (!g_cleared) {
        g_cleared = 1;
        ss_menu_reached();
    }
}

void loader_menu_frame(void)
{
    if (g_clear == SS_CLEAR_AT_FRAME)
        clear_marker();
}

/* post_init: the menus are set up; the marker goes here only when no menu frame will tell. */
static void __cdecl on_menu_up(RonEvent ev, void *game_object, void *user)
{
    (void)ev;
    (void)game_object;
    (void)user;
    if (g_clear == SS_CLEAR_AT_POST_INIT)
        clear_marker();
}

static LoaderPlugin g_plugins[ORDER_MAX];
static RonPlugin g_handles[ORDER_MAX];
static wchar_t g_folders[ORDER_MAX][MAX_PATH];
static int g_count;
static RonGameInfo g_game;
static wchar_t g_game_dir[MAX_PATH];
static wchar_t g_plugins_dir[MAX_PATH];

const LoaderPlugin *loader_plugins(int *count)
{
    if (count)
        *count = g_count;
    return g_plugins;
}

const RonGameInfo *loader_game(void) { return &g_game; }

const char *loader_owner_name(int owner)
{
    if (owner >= 1 && owner <= g_count)
        return g_plugins[owner - 1].m.name;
    return "loader";
}

const wchar_t *loader_owner_folder(int owner)
{
    if (owner >= 1 && owner <= g_count && g_folders[owner - 1][0])
        return g_folders[owner - 1];
    return NULL;
}

const wchar_t *loader_owner_id(int owner)
{
    if (owner >= 1 && owner <= g_count)
        return g_plugins[owner - 1].id;
    return L"loader";
}

static void to_utf8(const wchar_t *w, char *out, int cap)
{
    if (WideCharToMultiByte(CP_UTF8, 0, w, -1, out, cap, NULL, NULL) <= 0)
        out[0] = 0;
}

static void on_skip(const wchar_t *dll)
{
    char name[128];
    to_utf8(dll, name, sizeof name);
    size_t n = strlen(name);
    if (n > 4)
        name[n - 4] = 0;  /* the id, for the message */
    log_line("loader", "%s.dll has no %s.ini, not a plugin", name, name);
}

static char *read_whole(const wchar_t *path, size_t *len)
{
    FILE *f = _wfopen(path, L"rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = n >= 0 && n < 4 * 1024 * 1024 ? malloc((size_t)n + 1) : NULL;
    if (buf) {
        *len = fread(buf, 1, (size_t)n, f);
        buf[*len] = 0;
    }
    fclose(f);
    return buf;
}

/* Settles the load order and writes load-order.txt when its text changes. */
static int settle_order(OrderItem *items)
{
    static wchar_t found[ORDER_MAX][64];
    int n_found = order_discover(g_plugins_dir, found, ORDER_MAX, on_skip);
    wchar_t path[MAX_PATH];
    _snwprintf(path, MAX_PATH, L"%ls\\load-order.txt", g_plugins_dir);
    size_t len = 0;
    char *old = read_whole(path, &len);
    static OrderItem listed[ORDER_MAX];
    int n_listed = old ? order_parse(old, len, listed, ORDER_MAX) : 0;
    int dropped = 0;
    int n = order_merge(listed, n_listed, (const wchar_t(*)[64])found, n_found, items, ORDER_MAX, &dropped);
    if (dropped)
        log_line("loader", "%d listed plugins no longer present", dropped);
    static char text[ORDER_MAX * 200 + 256];
    size_t tl = order_serialize(items, n, text, sizeof text);
    if (tl && (!old || len != tl || memcmp(old, text, tl) != 0)) {
        FILE *f = _wfopen(path, L"wb");
        if (f) {
            fwrite(text, 1, tl, f);
            fclose(f);
        } else {
            log_line("loader", "could not write load-order.txt");
        }
    }
    free(old);
    return n;
}

static void fail(LoaderPlugin *p, const char *reason)
{
    p->state = LP_FAILED;
    snprintf(p->reason, sizeof p->reason, "%s", reason);
    log_line("loader", "plugin %s failed: %s", p->m.name, reason);
}

/* Reads the ini and checks the plugin's Folder; no plugin code runs here. */
static void prepare(int i, const OrderItem *item)
{
    LoaderPlugin *p = &g_plugins[i];
    memset(p, 0, sizeof *p);
    memcpy(p->id, item->name, sizeof p->id);
    p->enabled = item->enabled;
    p->is_new = item->is_new;
    p->state = item->enabled ? LP_FAILED : LP_DISABLED;
    g_handles[i].owner = i + 1;
    g_folders[i][0] = 0;

    wchar_t ini[MAX_PATH];
    _snwprintf(ini, MAX_PATH, L"%ls\\%ls.ini", g_plugins_dir, p->id);
    size_t len = 0;
    char *text = read_whole(ini, &len);
    if (text) {
        manifest_parse(text, len, &p->m);
        free(text);
    } else {
        manifest_parse("", 0, &p->m);
    }
    if (!p->m.name[0])
        to_utf8(p->id, p->m.name, sizeof p->m.name);
    if (!p->enabled)
        return;

    char reason[160];
    if (!text) {
        fail(p, "cannot read its .ini");
        return;
    }
    if (p->m.folder_bad) {
        fail(p, "bad Folder value");
        return;
    }
    if (!p->m.folder[0])
        return;
    wchar_t wfolder[64];
    MultiByteToWideChar(CP_UTF8, 0, p->m.folder, -1, wfolder, 64);
    _snwprintf(g_folders[i], MAX_PATH, L"%ls\\%ls", g_plugins_dir, wfolder);
    DWORD attr = GetFileAttributesW(g_folders[i]);
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        g_folders[i][0] = 0;
        snprintf(reason, sizeof reason, "folder %s not found", p->m.folder);
        fail(p, reason);
        return;
    }
    for (int j = 0; j < i; j++) {
        if (g_plugins[j].enabled && g_folders[j][0] &&
            CompareStringOrdinal(g_folders[j], -1, g_folders[i], -1, TRUE) == CSTR_EQUAL) {
            g_folders[i][0] = 0;
            snprintf(reason, sizeof reason, "folder %s already used by %s", p->m.folder, g_plugins[j].m.name);
            fail(p, reason);
            return;
        }
    }
}

static int load_plugin(int i)
{
    LoaderPlugin *p = &g_plugins[i];
    int owner = i + 1;
    char reason[96];
    wchar_t dll[MAX_PATH];
    _snwprintf(dll, MAX_PATH, L"%ls\\%ls.dll", g_plugins_dir, p->id);
    log_line("loader", "loading %s", p->m.name);  /* so a crash inside the plugin points at it */
    HMODULE mod = LoadLibraryExW(dll, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!mod) {
        snprintf(reason, sizeof reason, "LoadLibrary error %lu", GetLastError());
        fail(p, reason);
        return 0;
    }
    RonPluginMainFn main_fn = (RonPluginMainFn)(void *)GetProcAddress(mod, "RonPluginMain");
    if (!main_fn) {
        FreeLibrary(mod);
        fail(p, "no RonPluginMain");
        return 0;
    }
    /* its Folder is served from the start, as if it had called file_folder first (design section 9) */
    if (g_folders[i][0]) {
        int files = 0;
        if (fs_folder(owner, g_folders[i], &files) != RON_OK)
            log_line("loader", "%s: its folder could not be read in full", p->m.name);
        log_line("loader", "%s serves %d files from its folder", p->m.name, files);
    }
    RonPluginInfo info = {0, 0};
    int ok = main_fn(api_table(), &g_handles[i], &info);
    reason[0] = 0;
    if (info.api_major != RON_API_MAJOR)
        snprintf(reason, sizeof reason, "API version %u", (unsigned)info.api_major);
    else if (!ok)
        snprintf(reason, sizeof reason, "returned 0");
    if (reason[0]) {
        hm_rollback_owner(owner);
        ev_drop_owner(owner);
        fs_drop_owner(owner);
        FreeLibrary(mod);
        fail(p, reason);
        return 0;
    }
    p->state = LP_LOADED;
    p->reason[0] = 0;
    p->m.affects_sim = p->m.affects_sim || info.affects_sim;
    log_line("loader", "plugin %s %s loaded", p->m.name, p->m.version[0] ? p->m.version : "(no version)");
    return 1;
}

int loader_run(const LoaderConfig *cfg)
{
    g_count = 0;
    g_safe = 0;
    g_clear = SS_CLEAR_AT_POST_INIT;
    g_cleared = 0;
    g_game = cfg->game;
    wcsncpy(g_game_dir, cfg->game.game_dir ? cfg->game.game_dir : L".", MAX_PATH - 1);
    g_game.game_dir = g_game_dir;
    _snwprintf(g_plugins_dir, MAX_PATH, L"%ls\\plugins", g_game_dir);
    CreateDirectoryW(g_plugins_dir, NULL);

    wchar_t path[MAX_PATH];
    _snwprintf(path, MAX_PATH, L"%ls\\loader.log", g_plugins_dir);
    log_open(path);
    const char *kind = g_game.exe == RON_EXE_STOCK ? "stock" : g_game.exe == RON_EXE_PATCHED ? "patched" : "unknown";
    log_line("loader", "RoN Plugin Loader %s", LOADER_VERSION_STRING);
    log_line("loader", "game %s %08x", kind, (unsigned)g_game.timestamp);
    if (g_game.exe == RON_EXE_UNKNOWN) {
        log_line("loader", "unknown game build, no plugins loaded");
        return 0;
    }
    _snwprintf(path, MAX_PATH, L"%ls\\winmm.dll", g_game_dir);
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) {
        log_line("loader", "old RON Revived winmm.dll found; uninstall it. No plugins loaded");
        return 0;
    }

    static OrderItem items[ORDER_MAX];
    int n = settle_order(items);
    for (int i = 0; i < n; i++)
        prepare(i, &items[i]);
    g_count = n;

    /* Safe start: the marker of a start that never reached the menu means no plugins this time. */
    wchar_t culprit[64];
    g_safe = ss_begin(g_plugins_dir, culprit, 64);
    if (g_safe) {
        int who = -1;
        for (int i = 0; i < n && culprit[0]; i++)
            if (g_plugins[i].enabled && _wcsicmp(g_plugins[i].id, culprit) == 0)
                who = i;
        if (who >= 0) {
            log_line("loader", "the last start did not reach the menu: no plugins this start (it stopped while loading %s)",
                     g_plugins[who].m.name);
            fail(&g_plugins[who], "the game stopped while it loaded");
        } else {
            log_line("loader", "the last start did not reach the menu: no plugins this start");
        }
    }

    _snwprintf(path, MAX_PATH, L"%ls\\sbl\\rise_z.map", g_game_dir);
    LARGE_INTEGER f, t0, t1;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    int names = sym_load(path, g_game.timestamp);
    QueryPerformanceCounter(&t1);
    if (names >= 0)
        log_line("loader", "symbols: %d names in %u ms", names,
                 (unsigned)((t1.QuadPart - t0.QuadPart) * 1000 / f.QuadPart));
    else
        log_line("loader", "symbols: sbl\\rise_z.map missing or for another build; api->sym finds nothing");

    if (cfg->install_game_hooks)
        log_line("loader", "event hooks: %d of 8 installed", ev_install_game_hooks(g_game.delta));
    /* generated files: emptied at every start */
    _snwprintf(path, MAX_PATH, L"%ls\\.cache\\gen", g_plugins_dir);
    fs_init(g_game_dir, path);

    hm_set_notify(api_notify);
    ev_register(0, RON_EV_POST_INIT, on_menu_up, NULL);
    int loaded = 0;
    if (!g_safe) {
        for (int i = 0; i < n; i++) {
            if (g_plugins[i].enabled && !g_plugins[i].reason[0] && g_plugins[i].state != LP_DISABLED) {
                ss_loading(g_plugins[i].id);
                loaded += load_plugin(i);
            }
        }
        ss_loaded();
    }
    /* The file service's hooks only when some plugin serves files: with none, the game's file calls
       stay untouched. Before the lock, so they take their place in the chains like any hook. */
    if (fs_active() && cfg->install_game_hooks)
        log_line("loader", "file hooks: %d of 7 installed", fs_win32_install() + fs_game_install(g_game.delta));
    else if (fs_active() && cfg->install_file_hooks)
        log_line("loader", "file hooks: %d of 6 installed", fs_win32_install());
    /* The loader's menu hooks come after every plugin's, so they head their chains and no plugin can
       take the Plugins panel away (design section 8). */
    int frame_hook = 0;
    if (cfg->install_game_hooks)
        log_line("loader", "menu hooks: %d of 6 installed", menu_install(g_game.delta, &frame_hook));
    g_clear = ss_clear_point(cfg->install_game_hooks, frame_hook, ev_installed(RON_EV_POST_INIT));
    if (g_clear == SS_CLEAR_NOW) {
        log_line("loader", "no menu hook to tell that the menu is up: the start counts as good now");
        clear_marker();
    }
    /* Hooks only inside RonPluginMain: a hook made from pre_init would land on top of every chain and
       run before lower plugins' hooks. Event registration stays open through pre_init. */
    hm_lock();
    ev_fire(RON_EV_PRE_INIT, NULL);
    ev_lock();
    fs_lock();
    log_line("loader", "%d of %d plugins loaded", loaded, n);
    return loaded;
}

void loader_start(void)
{
    const uint8_t *image = (const uint8_t *)GetModuleHandleW(NULL);
    static wchar_t dir[MAX_PATH];
    GetModuleFileNameW(NULL, dir, MAX_PATH);
    wchar_t *slash = wcsrchr(dir, L'\\');
    if (slash)
        *slash = 0;
    LoaderConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.game.exe = identity_kind(image, &cfg.game.timestamp);
    cfg.game.delta = identity_delta(image);
    cfg.game.game_dir = dir;
    cfg.install_game_hooks = 1;
    loader_run(&cfg);
}

int loader_safe_start(void) { return g_safe; }
