#include <windows.h>
#include <string.h>
#include <wchar.h>
#include "check.h"
#include "loader/order.h"

static wchar_t g_skipped[8][64];
static int g_nskipped;

static void on_skip(const wchar_t *dll)
{
    if (g_nskipped < 8)
        wcsncpy(g_skipped[g_nskipped++], dll, 63);
}

static void make_dir(wchar_t *out, const wchar_t *name)
{
    GetTempPathW(MAX_PATH, out);
    wcscat(out, name);
    CreateDirectoryW(out, NULL);
}

static void touch(const wchar_t *dir, const wchar_t *rel)
{
    wchar_t p[MAX_PATH];
    swprintf(p, MAX_PATH, L"%ls\\%ls", dir, rel);
    HANDLE h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    CloseHandle(h);
}

static void rm(const wchar_t *dir, const wchar_t *rel)
{
    wchar_t p[MAX_PATH];
    swprintf(p, MAX_PATH, L"%ls\\%ls", dir, rel);
    DeleteFileW(p);
}

TEST(discover_sorted_requires_ini)
{
    wchar_t dir[MAX_PATH], sub[MAX_PATH];
    make_dir(dir, L"ron_order_test");
    swprintf(sub, MAX_PATH, L"%ls\\e", dir);
    CreateDirectoryW(sub, NULL);
    const wchar_t *files[] = {L"b.dll", L"b.ini", L"A.dll", L"A.ini", L"c.dll", L"d.ini", L"e\\e.dll", L"e\\e.ini"};
    for (int i = 0; i < 8; i++)
        touch(dir, files[i]);

    wchar_t names[ORDER_MAX][64];
    g_nskipped = 0;
    int n = order_discover(dir, names, ORDER_MAX, on_skip);
    CHECK_EQ(n, 2);
    CHECK(n == 2 && wcscmp(names[0], L"A") == 0 && wcscmp(names[1], L"b") == 0);
    CHECK_EQ(g_nskipped, 1);
    CHECK(g_nskipped == 1 && wcscmp(g_skipped[0], L"c.dll") == 0);

    for (int i = 0; i < 8; i++)
        rm(dir, files[i]);
    RemoveDirectoryW(sub);
    RemoveDirectoryW(dir);
}

TEST(discover_non_ascii)
{
    wchar_t dir[MAX_PATH];
    make_dir(dir, L"тест-ü");
    touch(dir, L"Ünï.dll");
    touch(dir, L"Ünï.ini");
    wchar_t names[ORDER_MAX][64];
    int n = order_discover(dir, names, ORDER_MAX, NULL);
    CHECK_EQ(n, 1);
    CHECK(n == 1 && wcscmp(names[0], L"Ünï") == 0);
    rm(dir, L"Ünï.dll");
    rm(dir, L"Ünï.ini");
    RemoveDirectoryW(dir);
}

TEST(parse_rules)
{
    const char *text = "\xEF\xBB\xBF# x\r\n\r\n  -Foo  \r\nfoo\r\n";
    OrderItem items[ORDER_MAX];
    int n = order_parse(text, strlen(text), items, ORDER_MAX);
    CHECK_EQ(n, 1);
    CHECK(n == 1 && wcscmp(items[0].name, L"Foo") == 0);
    CHECK_EQ(items[0].enabled, 0);
}

TEST(merge_new_appended)
{
    OrderItem listed[1] = {{L"B", 1, 0}};
    const wchar_t found[2][64] = {L"A", L"B"};
    OrderItem out[ORDER_MAX];
    int dropped = -1;
    int n = order_merge(listed, 1, found, 2, out, ORDER_MAX, &dropped);
    CHECK_EQ(n, 2);
    CHECK_EQ(dropped, 0);
    CHECK(wcscmp(out[0].name, L"B") == 0 && out[0].is_new == 0 && out[0].enabled == 1);
    CHECK(wcscmp(out[1].name, L"A") == 0 && out[1].is_new == 1 && out[1].enabled == 1);
}

TEST(merge_missing_dropped)
{
    OrderItem listed[1] = {{L"X", 1, 0}};
    OrderItem out[ORDER_MAX];
    int dropped = 0;
    int n = order_merge(listed, 1, NULL, 0, out, ORDER_MAX, &dropped);
    CHECK_EQ(n, 0);
    CHECK_EQ(dropped, 1);
}

TEST(serialize_roundtrip)
{
    OrderItem items[3] = {{L"A", 1, 0}, {L"B", 0, 0}, {L"Ünï", 1, 1}};
    char buf[1024];
    size_t len = order_serialize(items, 3, buf, sizeof buf);
    CHECK(len > 0);
    CHECK(strstr(buf, "# Load order: top loads first; the lowest plugin wins. A leading - turns a plugin off.\r\n") == buf);
    CHECK(strstr(buf, "\r\n-B\r\n") != NULL);
    OrderItem back[ORDER_MAX];
    int n = order_parse(buf, len, back, ORDER_MAX);
    CHECK_EQ(n, 3);
    for (int i = 0; i < 3 && i < n; i++) {
        CHECK(wcscmp(back[i].name, items[i].name) == 0);
        CHECK_EQ(back[i].enabled, items[i].enabled);
    }
}
