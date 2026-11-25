#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "check.h"
#include "loader/loader.h"
#include "loader/hooks.h"
#include "loader/events.h"
#include "loader/log.h"
#include "loader/files.h"
#include "common/version.h"

int hk_add1(int x);

/* A fresh game folder in %TEMP% with a copy of build\<fixture>\plugins (and plugins\GoodFiles). */
static void make_game_from(wchar_t *dir, const wchar_t *fixture)
{
    static int counter;
    wchar_t src[MAX_PATH], pattern[MAX_PATH], plugins[MAX_PATH];
    GetModuleFileNameW(NULL, src, MAX_PATH);
    *wcsrchr(src, L'\\') = 0;
    wcscat(src, L"\\");
    wcscat(src, fixture);
    wcscat(src, L"\\plugins");
    GetTempPathW(MAX_PATH, dir);
    swprintf(dir + wcslen(dir), 64, L"ron_loader_game_%lu_%d", GetCurrentProcessId(), ++counter);
    CreateDirectoryW(dir, NULL);
    swprintf(plugins, MAX_PATH, L"%ls\\plugins", dir);
    CreateDirectoryW(plugins, NULL);
    swprintf(pattern, MAX_PATH, L"%ls\\*", src);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            wchar_t a[MAX_PATH], b[MAX_PATH];
            swprintf(a, MAX_PATH, L"%ls\\%ls", src, fd.cFileName);
            swprintf(b, MAX_PATH, L"%ls\\%ls", plugins, fd.cFileName);
            CopyFileW(a, b, FALSE);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    wcscat(plugins, L"\\GoodFiles");
    CreateDirectoryW(plugins, NULL);
}

static void make_game(wchar_t *dir) { make_game_from(dir, L"testgame"); }

static int run(const wchar_t *dir, RonExeKind kind)
{
    hm_reset();
    ev_reset();
    LoaderConfig cfg = {{kind, 0x6674863F, 0, dir}, 0};
    int n = loader_run(&cfg);
    log_close();
    return n;
}

static size_t read_file(const wchar_t *dir, const wchar_t *rel, char *buf, size_t cap)
{
    wchar_t p[MAX_PATH];
    swprintf(p, MAX_PATH, L"%ls\\%ls", dir, rel);
    FILE *f = _wfopen(p, L"rb");
    if (!f)
        return 0;
    size_t n = fread(buf, 1, cap - 1, f);
    fclose(f);
    buf[n] = 0;
    return n;
}

static void write_file(const wchar_t *dir, const wchar_t *rel, const char *text)
{
    wchar_t p[MAX_PATH];
    swprintf(p, MAX_PATH, L"%ls\\%ls", dir, rel);
    FILE *f = _wfopen(p, L"wb");
    fputs(text, f);
    fclose(f);
}

static const LoaderPlugin *find(const wchar_t *id)
{
    int n = 0;
    const LoaderPlugin *p = loader_plugins(&n);
    for (int i = 0; i < n; i++) {
        if (wcscmp(p[i].id, id) == 0)
            return &p[i];
    }
    return NULL;
}

static void check_failed(const wchar_t *id, const char *reason)
{
    const LoaderPlugin *p = find(id);
    CHECK(p != NULL);
    if (!p)
        return;
    CHECK_EQ(p->state, LP_FAILED);
    CHECK_STR(p->reason, reason);
}

TEST(run_loads_good_only)
{
    wchar_t dir[MAX_PATH];
    make_game(dir);
    CHECK_EQ(run(dir, RON_EXE_STOCK), 1);
    const LoaderPlugin *good = find(L"Good");
    CHECK(good && good->state == LP_LOADED);
    check_failed(L"Zero", "returned 0");
    check_failed(L"BadMajor", "API version 99");
    check_failed(L"Garbage", "LoadLibrary error 193");
    check_failed(L"MissingFolder", "folder Nope not found");
    check_failed(L"Twin", "folder GoodFiles already used by Good");
    CHECK(find(L"NoIni") == NULL);
    int n = 0;
    loader_plugins(&n);
    CHECK_EQ(n, 6);
    static char log[16384];
    read_file(dir, L"plugins\\loader.log", log, sizeof log);
    CHECK(strstr(log, "[loader] RoN Plugin Loader " LOADER_VERSION_STRING "\r\n") != NULL);
    CHECK(strstr(log, "NoIni.dll has no NoIni.ini") != NULL);
    CHECK(strstr(log, "[Good] pre_init") != NULL);
    CHECK(strstr(log, "plugin Good 1.0 loaded") != NULL);
    CHECK(strstr(log, "loading Good") != NULL);  /* written before its code runs */
    CHECK(strstr(log, "plugin Zero failed: returned 0") != NULL);
}

TEST(run_zero_rolled_back)
{
    wchar_t dir[MAX_PATH];
    make_game(dir);
    run(dir, RON_EXE_STOCK);
    CHECK_EQ(hk_add1(1), 2);
}

TEST(run_writes_order)
{
    wchar_t dir[MAX_PATH];
    make_game(dir);
    run(dir, RON_EXE_STOCK);
    static char first[4096], second[4096];
    size_t n1 = read_file(dir, L"plugins\\load-order.txt", first, sizeof first);
    CHECK_STR(first, "# Load order: top loads first; the lowest plugin wins. A leading - turns a plugin off.\r\n"
                     "BadMajor\r\nGarbage\r\nGood\r\nMissingFolder\r\nTwin\r\nZero\r\n");
    ev_fire(RON_EV_POST_INIT, NULL);  /* the menu was reached, so the next start is a normal one */
    run(dir, RON_EXE_STOCK);
    size_t n2 = read_file(dir, L"plugins\\load-order.txt", second, sizeof second);
    CHECK_EQ(n1, n2);
    CHECK(memcmp(first, second, n1) == 0);
}

TEST(run_disabled_skipped)
{
    wchar_t dir[MAX_PATH];
    make_game(dir);
    write_file(dir, L"plugins\\load-order.txt", "-Good\r\n");
    /* Good is off, so its folder is free: Twin (a copy of Good naming the same folder) now loads */
    CHECK_EQ(run(dir, RON_EXE_STOCK), 1);
    const LoaderPlugin *good = find(L"Good");
    CHECK(good && good->state == LP_DISABLED);
    const LoaderPlugin *twin = find(L"Twin");
    CHECK(twin && twin->state == LP_LOADED);
}

TEST(run_old_winmm)
{
    wchar_t dir[MAX_PATH];
    make_game(dir);
    write_file(dir, L"winmm.dll", "x");
    CHECK_EQ(run(dir, RON_EXE_STOCK), 0);
    static char log[4096];
    read_file(dir, L"plugins\\loader.log", log, sizeof log);
    CHECK(strstr(log, "old RON Revived winmm.dll found; uninstall it. No plugins loaded") != NULL);
}

TEST(run_unknown_exe)
{
    wchar_t dir[MAX_PATH];
    make_game(dir);
    CHECK_EQ(run(dir, RON_EXE_UNKNOWN), 0);
    static char log[4096];
    read_file(dir, L"plugins\\loader.log", log, sizeof log);
    CHECK(strstr(log, "unknown game build, no plugins loaded") != NULL);
}

/* Hooks are accepted only inside RonPluginMain: one made from a pre_init callback is refused, so it
   cannot jump ahead of lower plugins in a chain. */
TEST(run_late_hook_refused)
{
    wchar_t dir[MAX_PATH];
    make_game_from(dir, L"testgame2");
    run(dir, RON_EXE_STOCK);
    static char log[8192];
    read_file(dir, L"plugins\\loader.log", log, sizeof log);
    CHECK(strstr(log, "[LateHook] late hook 4") != NULL);  /* RON_E_STATE */
    CHECK_EQ(hk_add1(1), 2);
}

/* A plugin written in C++ is found through the header alone (RonPluginMain keeps its C name). */
TEST(run_cpp_plugin_loads)
{
    wchar_t dir[MAX_PATH];
    make_game_from(dir, L"testgame2");
    run(dir, RON_EXE_STOCK);
    const LoaderPlugin *cpp = find(L"CppGood");
    CHECK(cpp && cpp->state == LP_LOADED);
    static char log[8192];
    read_file(dir, L"plugins\\loader.log", log, sizeof log);
    CHECK(strstr(log, "[CppGood] cpp loaded") != NULL);
}

TEST(run_safe_start_after_stop)
{
    wchar_t dir[MAX_PATH];
    make_game(dir);
    write_file(dir, L"plugins\\.start-marker", "loading Good\r\n");
    CHECK_EQ(run(dir, RON_EXE_STOCK), 0);
    CHECK_EQ(loader_safe_start(), 1);
    const LoaderPlugin *good = find(L"Good");
    CHECK(good != NULL);
    if (good)
        CHECK_STR(good->reason, "the game stopped while it loaded");
    static char log[16384];
    read_file(dir, L"plugins\\loader.log", log, sizeof log);
    CHECK(strstr(log, "the last start did not reach the menu: no plugins this start (it stopped while loading Good)") != NULL);
}

TEST(run_normal_after_menu)
{
    wchar_t dir[MAX_PATH];
    make_game(dir);
    run(dir, RON_EXE_STOCK);
    ev_fire(RON_EV_POST_INIT, NULL);
    char text[64];
    CHECK_EQ(read_file(dir, L"plugins\\.start-marker", text, sizeof text), 0);
    CHECK_EQ(run(dir, RON_EXE_STOCK), 1);
    CHECK_EQ(loader_safe_start(), 0);
}

TEST(run_marker_after_loading)
{
    wchar_t dir[MAX_PATH];
    make_game(dir);
    run(dir, RON_EXE_STOCK);
    char text[64] = "";
    read_file(dir, L"plugins\\.start-marker", text, sizeof text);
    CHECK_STR(text, "loaded\r\n");
}

TEST(run_serves_the_plugins_folder)
{
    wchar_t dir[MAX_PATH], p[MAX_PATH];
    make_game(dir);
    swprintf(p, MAX_PATH, L"%ls\\plugins\\GoodFiles\\Data", dir);
    CreateDirectoryW(p, NULL);
    wcscat(p, L"\\g.xml");
    FILE *f = _wfopen(p, L"wb");
    if (f) {
        fputs("<g/>", f);
        fclose(f);
    }
    CHECK_EQ(run(dir, RON_EXE_STOCK), 1);
    const wchar_t *s = fs_resolve(L"data\\G.xml", NULL);
    CHECK(s && _wcsicmp(s, p) == 0);
    static char log[16384];
    read_file(dir, L"plugins\\loader.log", log, sizeof log);
    CHECK(strstr(log, "Good serves 1 files from its folder") != NULL);
    /* registrations close with pre_init */
    CHECK_EQ(fs_use(1, L"data\\h.xml", p), RON_E_STATE);
    fs_reset();
}
