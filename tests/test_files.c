/* The file service: names, claims and their order, generators, folder roots, and the KernelBase
 * hooks on a temporary game folder. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "check.h"
#include "loader/files.h"
#include "loader/files_win32.h"
#include "loader/hooks.h"

static wchar_t g_dir[MAX_PATH];  /* the fake game folder */

static void put(const wchar_t *rel, const char *text)
{
    wchar_t p[MAX_PATH];
    swprintf(p, MAX_PATH, L"%ls\\%ls", g_dir, rel);
    for (wchar_t *s = p + wcslen(g_dir) + 1; *s; s++) {
        if (*s == L'\\') {
            *s = 0;
            CreateDirectoryW(p, NULL);
            *s = L'\\';
        }
    }
    FILE *f = _wfopen(p, L"wb");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

static void fresh(void)
{
    static int counter;
    GetTempPathW(MAX_PATH, g_dir);
    swprintf(g_dir + wcslen(g_dir), 64, L"ron_files_%lu_%d", GetCurrentProcessId(), ++counter);
    CreateDirectoryW(g_dir, NULL);
    wchar_t gen[MAX_PATH];
    swprintf(gen, MAX_PATH, L"%ls\\plugins\\.cache\\gen", g_dir);
    fs_init(g_dir, gen);
}

static void path_of(const wchar_t *rel, wchar_t *out)
{
    swprintf(out, MAX_PATH, L"%ls\\%ls", g_dir, rel);
}

/* The text of a file read with CreateFileW (through the hooks when they are in). */
static void read_w(const wchar_t *path, char *buf, int cap)
{
    buf[0] = 0;
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return;
    DWORD got = 0;
    ReadFile(h, buf, (DWORD)cap - 1, &got, NULL);
    buf[got] = 0;
    CloseHandle(h);
}

TEST(files_norm)
{
    wchar_t r[FS_MAX_REL];
    CHECK(fs_norm(L"Data/rules.xml", r, FS_MAX_REL) && wcscmp(r, L"Data\\rules.xml") == 0);
    CHECK(fs_norm(L".\\art\\\\x.tga", r, FS_MAX_REL) && wcscmp(r, L"art\\x.tga") == 0);
    CHECK(fs_norm(L"masks.txt", r, FS_MAX_REL) && wcscmp(r, L"masks.txt") == 0);
    CHECK(fs_norm(L"scenario\\Scripts\\", r, FS_MAX_REL) && wcscmp(r, L"scenario\\Scripts") == 0);
    CHECK(!fs_norm(L"", r, FS_MAX_REL));
    CHECK(!fs_norm(L"C:\\x\\y.xml", r, FS_MAX_REL));
    CHECK(!fs_norm(L"\\Data\\x.xml", r, FS_MAX_REL));
    CHECK(!fs_norm(L"Data\\..\\x.xml", r, FS_MAX_REL));
    CHECK(!fs_norm(L"d3dgl.dll", r, FS_MAX_REL));
    CHECK(!fs_norm(L"art\\evil.EXE", r, FS_MAX_REL));
    CHECK(!fs_norm(L"plugins\\x.xml", r, FS_MAX_REL));
    CHECK(!fs_norm(L"MODS\\a\\Data\\x.xml", r, FS_MAX_REL));
    CHECK(!fs_norm(L"art\\*.tga", r, FS_MAX_REL));
    CHECK(fs_norm(L"art\\plugins\\x.tga", r, FS_MAX_REL));  /* only a first part named plugins is refused */
}

TEST(files_lowest_wins)
{
    fresh();
    put(L"plugins\\A\\Data\\rules.xml", "A");
    put(L"plugins\\A\\art\\a.tga", "A");
    put(L"plugins\\B\\data\\RULES.XML", "B");
    put(L"plugins\\B\\x.dll", "code");  /* never served */
    wchar_t a[MAX_PATH], b[MAX_PATH];
    path_of(L"plugins\\A", a);
    path_of(L"plugins\\B", b);
    int n = -1;
    CHECK(!fs_active());
    CHECK_EQ(fs_folder(1, a, &n), RON_OK);
    CHECK_EQ(n, 2);
    CHECK(fs_active());
    CHECK_EQ(fs_folder(2, b, &n), RON_OK);
    CHECK_EQ(n, 1);
    const wchar_t *p = fs_resolve(L"Data\\Rules.xml", NULL);
    CHECK(p && wcsstr(p, L"\\B\\") != NULL);  /* plugin 2 sits below plugin 1: it wins */
    p = fs_resolve(L"art\\a.tga", NULL);
    CHECK(p && wcsstr(p, L"\\A\\") != NULL);
    CHECK(fs_resolve(L"art\\none.tga", NULL) == NULL);
    CHECK(fs_resolve(L"x.dll", NULL) == NULL);
    CHECK_EQ(fs_owner_files(1), 2);
    CHECK_EQ(fs_owner_files(2), 1);
    /* a plugin that fails to load takes its files with it */
    fs_drop_owner(2);
    p = fs_resolve(L"data\\rules.xml", NULL);
    CHECK(p && wcsstr(p, L"\\A\\") != NULL);
    CHECK_EQ(fs_owner_files(2), 0);
    fs_lock();
    CHECK_EQ(fs_folder(3, b, &n), RON_E_STATE);
    CHECK_EQ(fs_use(3, L"data\\x.xml", a), RON_E_STATE);
    fs_reset();
}

TEST(files_use_later_call_wins_and_needs_the_file)
{
    fresh();
    put(L"one.txt", "1");
    put(L"two.txt", "2");
    wchar_t one[MAX_PATH], two[MAX_PATH], gone[MAX_PATH];
    path_of(L"one.txt", one);
    path_of(L"two.txt", two);
    path_of(L"gone.txt", gone);
    CHECK_EQ(fs_use(1, L"Data\\t.xml", one), RON_OK);
    CHECK_EQ(fs_use(1, L"Data\\t.xml", two), RON_OK);
    const wchar_t *p = fs_resolve(L"data\\t.xml", NULL);
    CHECK(p && wcscmp(p, two) == 0);
    CHECK_EQ(fs_use(1, L"Data\\u.xml", gone), RON_OK);
    CHECK(fs_resolve(L"data\\u.xml", NULL) == NULL);  /* a used file that is gone falls back */
    CHECK_EQ(fs_use(1, L"x.exe", one), RON_E_ARG);
    CHECK_EQ(fs_use(1, L"Data\\t.xml", L""), RON_E_ARG);
    fs_reset();
}

static int g_gen_calls;
static int __cdecl gen_append(const char *rel, const wchar_t *in, const wchar_t *out, void *user)
{
    g_gen_calls++;
    char text[256] = {0};
    FILE *f = _wfopen(in, L"rb");
    if (f) {
        fread(text, 1, sizeof text - 1, f);
        fclose(f);
    }
    f = _wfopen(out, L"wb");
    if (!f)
        return 0;
    fprintf(f, "%s+%s(%s)", text, (const char *)user, rel);
    fclose(f);
    return 1;
}

static int __cdecl gen_none(const char *rel, const wchar_t *in, const wchar_t *out, void *user)
{
    (void)rel;
    (void)in;
    (void)out;
    (void)user;
    g_gen_calls++;
    return 0;
}

TEST(files_generate_builds_on_the_plugins_above)
{
    fresh();
    put(L"Data\\u.xml", "game");
    put(L"plugins\\A\\Data\\u.xml", "A");
    wchar_t a[MAX_PATH];
    path_of(L"plugins\\A", a);
    char buf[256];
    g_gen_calls = 0;
    CHECK_EQ(fs_generate(3, L"Gen", L"Data\\u.xml", gen_append, "g3"), RON_OK);
    CHECK_EQ(fs_folder(1, a, NULL), RON_OK);
    CHECK_EQ(fs_generate(2, L"Mid", L"data\\u.xml", gen_append, "g2"), RON_OK);
    const wchar_t *p = fs_resolve(L"Data\\u.xml", NULL);
    CHECK(p && wcsstr(p, L"\\.cache\\gen\\Gen\\Data\\u.xml") != NULL);
    read_w(p, buf, sizeof buf);
    CHECK_STR(buf, "A+g2(Data\\u.xml)+g3(Data\\u.xml)");
    CHECK_EQ(g_gen_calls, 2);
    CHECK(fs_resolve(L"Data\\u.xml", NULL) == p);  /* once per start */
    CHECK_EQ(g_gen_calls, 2);
    /* the game's own copy (or its mods' answer, `base`) when no plugin above serves it */
    put(L"mods\\M\\Data\\v.xml", "mod");
    CHECK_EQ(fs_generate(1, L"One", L"Data\\v.xml", gen_append, "g1"), RON_OK);
    p = fs_resolve(L"Data\\v.xml", L"mods\\M\\Data\\v.xml");
    read_w(p, buf, sizeof buf);
    CHECK_STR(buf, "mod+g1(Data\\v.xml)");
    /* a generator that writes nothing leaves the file as it was */
    CHECK_EQ(fs_generate(1, L"One", L"Data\\w.xml", gen_none, NULL), RON_OK);
    CHECK(fs_resolve(L"Data\\w.xml", NULL) == NULL);
    fs_reset();
}

TEST(files_dir_roots_in_load_order)
{
    fresh();
    put(L"plugins\\A\\scenario\\Scripts\\Sa\\a.bhs", "a");
    put(L"plugins\\B\\scenario\\Scripts\\Sb\\b.bhs", "b");
    wchar_t a[MAX_PATH], b[MAX_PATH];
    path_of(L"plugins\\A", a);
    path_of(L"plugins\\B", b);
    fs_folder(2, b, NULL);
    fs_folder(1, a, NULL);
    const wchar_t *roots[4];
    CHECK_EQ(fs_dir_roots(L"Scenario\\scripts", roots, 4), 2);
    CHECK(wcscmp(roots[0], a) == 0 && wcscmp(roots[1], b) == 0);
    CHECK_EQ(fs_dir_roots(L"scenario\\Scripts\\Sb", roots, 4), 1);
    CHECK_EQ(fs_dir_roots(L"", roots, 4), 0);
    fs_reset();
}

static int listing(const wchar_t *pattern, char *names, int cap)
{
    names[0] = 0;
    WIN32_FIND_DATAW d;
    HANDLE h = FindFirstFileW(pattern, &d);
    if (h == INVALID_HANDLE_VALUE)
        return -1;
    int n = 0;
    do {
        if (d.cFileName[0] == L'.')
            continue;
        char a[MAX_PATH];
        WideCharToMultiByte(CP_UTF8, 0, d.cFileName, -1, a, sizeof a, NULL, NULL);
        if ((int)(strlen(names) + strlen(a) + 2) < cap) {
            strcat(names, a);
            strcat(names, ";");
        }
        n++;
    } while (FindNextFileW(h, &d));
    FindClose(h);
    return n;
}

typedef HANDLE(WINAPI *CreateW)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static CreateW g_plugin_create_orig;
static int g_plugin_creates;
static HANDLE WINAPI plugin_create(LPCWSTR n, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD d, DWORD f, HANDLE t)
{
    g_plugin_creates++;
    return g_plugin_create_orig(n, a, s, sa, d, f, t);
}

TEST(files_win32_hooks_serve_reads_and_merge_listings)
{
    fresh();
    put(L"Data\\a.txt", "game a");
    put(L"Data\\b.txt", "game b");
    put(L"top.txt", "game top");
    put(L"plugins\\P\\Data\\a.txt", "plugin a");
    put(L"plugins\\P\\Data\\new.txt", "plugin new");
    put(L"plugins\\P\\top.txt", "plugin top");
    put(L"plugins\\P\\settings.ini", "x=1");
    put(L"plugins\\P\\scenario\\Scripts\\Surv\\s.bhs", "script");
    wchar_t root[MAX_PATH], p[MAX_PATH];
    path_of(L"plugins\\P", root);
    hm_reset();
    fs_win32_forget();
    CHECK_EQ(fs_folder(1, root, NULL), RON_OK);
    /* a plugin hooked CreateFileW first (RON Revived does): the loader's link still goes on */
    static const uint8_t prologue[5] = {0x8B, 0xFF, 0x55, 0x8B, 0xEC};
    CHECK_EQ(hm_inline(1, (uintptr_t)GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "CreateFileW"), prologue,
                       NULL, 5, 5, (void *)plugin_create, (void **)&g_plugin_create_orig),
             RON_OK);
    CHECK_EQ(fs_win32_install(), 6);
    g_plugin_creates = 0;
    char buf[256], names[512];
    path_of(L"Data\\a.txt", p);
    read_w(p, buf, sizeof buf);
    CHECK_STR(buf, "plugin a");
    CHECK(g_plugin_creates >= 1);  /* the plugin's link still runs, under the loader's */
    path_of(L"Data\\b.txt", p);
    read_w(p, buf, sizeof buf);
    CHECK_STR(buf, "game b");
    path_of(L"top.txt", p);
    read_w(p, buf, sizeof buf);
    CHECK_STR(buf, "plugin top");  /* top-level data files are served too (masks.txt) */
    path_of(L"Data\\new.txt", p);
    CHECK(GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES);
    WIN32_FILE_ATTRIBUTE_DATA fa;
    CHECK(GetFileAttributesExW(p, GetFileExInfoStandard, &fa) && fa.nFileSizeLow == 10);
    /* a relative name, as the game passes them */
    wchar_t cwd[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, cwd);
    SetCurrentDirectoryW(g_dir);
    read_w(L"data/a.txt", buf, sizeof buf);
    CHECK_STR(buf, "plugin a");
    SetCurrentDirectoryW(cwd);
    /* a folder only the plugin has */
    path_of(L"scenario\\Scripts\\Surv", p);
    DWORD at = GetFileAttributesW(p);
    CHECK(at != INVALID_FILE_ATTRIBUTES && (at & FILE_ATTRIBUTE_DIRECTORY));
    /* listings: the game's entries and the plugin's added ones, once each */
    path_of(L"Data\\*", p);
    CHECK_EQ(listing(p, names, sizeof names), 3);
    CHECK(strstr(names, "a.txt;") && strstr(names, "b.txt;") && strstr(names, "new.txt;"));
    path_of(L"scenario\\Scripts\\*", p);
    CHECK_EQ(listing(p, names, sizeof names), 1);
    CHECK_STR(names, "Surv;");
    path_of(L"Data\\n*.txt", p);
    CHECK_EQ(listing(p, names, sizeof names), 1);
    path_of(L"Data\\zz*", p);
    CHECK_EQ(listing(p, names, sizeof names), -1);
    /* the game folder's own listing is never merged (the plugin's settings stay out of it) */
    path_of(L"*", p);
    listing(p, names, sizeof names);
    CHECK(strstr(names, "settings.ini") == NULL);
    /* writes stay where they were aimed */
    path_of(L"Data\\a.txt", p);
    HANDLE h = CreateFileW(p, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    CHECK(h != INVALID_HANDLE_VALUE);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD w;
        WriteFile(h, "G", 1, &w, NULL);
        CloseHandle(h);
    }
    path_of(L"plugins\\P\\Data\\a.txt", p);
    read_w(p, buf, sizeof buf);
    CHECK_STR(buf, "plugin a");
    hm_reset();
    fs_win32_forget();
    path_of(L"Data\\a.txt", p);
    read_w(p, buf, sizeof buf);
    CHECK_STR(buf, "Game a");  /* hooks gone: the game's own file, as written above */
    fs_reset();
}

/* ---- the hook manager's library window and a plugin's patches over its own ---- */

static uint8_t g_mem[64];

TEST(hooks_window_and_self_patch)
{
    hm_reset();
    memset(g_mem, 0x90, sizeof g_mem);
    uintptr_t at = (uintptr_t)g_mem;
    uint8_t nop4[4] = {0x90, 0x90, 0x90, 0x90}, a4[4] = {1, 2, 3, 4}, b2[2] = {9, 9};
    CHECK_EQ(hm_patch(1, at, nop4, a4, 4), RON_OK);
    uint8_t now2[2] = {3, 4};
    CHECK_EQ(hm_patch(1, at + 2, now2, b2, 2), RON_OK);  /* over its own patch */
    CHECK(g_mem[0] == 1 && g_mem[1] == 2 && g_mem[2] == 9 && g_mem[3] == 9);
    CHECK_EQ(hm_patch(2, at + 3, (uint8_t[]){9}, (uint8_t[]){7}, 1), RON_E_CONFLICT);  /* never over another's */
    hm_rollback_owner(1);
    CHECK(g_mem[0] == 0x90 && g_mem[1] == 0x90 && g_mem[2] == 0x90 && g_mem[3] == 0x90);
    hm_lock();
    CHECK_EQ(hm_patch(1, at + 8, nop4, a4, 4), RON_E_STATE);
    hm_window(at + 8, at + 16);
    CHECK_EQ(hm_patch(1, at + 8, nop4, a4, 4), RON_OK);
    CHECK_EQ(hm_patch(1, at + 14, nop4, a4, 4), RON_E_STATE);  /* reaches past the window */
    hm_window(0, 0);
    CHECK_EQ(hm_patch(1, at + 20, nop4, a4, 4), RON_E_STATE);
    hm_reset();
    CHECK(g_mem[8] == 0x90);
}
