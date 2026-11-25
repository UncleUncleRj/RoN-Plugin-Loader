#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "check.h"
#include "loader/safestart.h"

/* A fresh <temp>\ron_ss_<pid>_<n>\plugins folder. */
static void fresh_plugins(wchar_t *dir)
{
    static int counter;
    wchar_t base[MAX_PATH];
    GetTempPathW(MAX_PATH, base);
    swprintf(dir, MAX_PATH, L"%lsron_ss_%lu_%d", base, GetCurrentProcessId(), ++counter);
    CreateDirectoryW(dir, NULL);
    wcscat(dir, L"\\plugins");
    CreateDirectoryW(dir, NULL);
}

/* The marker's text, "" when it is missing. */
static const char *marker(const wchar_t *dir)
{
    static char text[256];
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%ls\\.start-marker", dir);
    text[0] = 0;
    FILE *f = _wfopen(path, L"rb");
    if (f) {
        size_t n = fread(text, 1, sizeof text - 1, f);
        text[n] = 0;
        fclose(f);
    }
    return text;
}

static int marker_exists(const wchar_t *dir)
{
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%ls\\.start-marker", dir);
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

TEST(ss_first_start_is_normal)
{
    wchar_t dir[MAX_PATH], culprit[64] = L"x";
    fresh_plugins(dir);
    CHECK_EQ(ss_begin(dir, culprit, 64), 0);
    CHECK(culprit[0] == 0);
    CHECK_STR(marker(dir), "starting\r\n");
}

TEST(ss_after_menu_is_normal)
{
    wchar_t dir[MAX_PATH], culprit[64];
    fresh_plugins(dir);
    ss_begin(dir, culprit, 64);
    ss_menu_reached();
    CHECK(!marker_exists(dir));
    CHECK_EQ(ss_begin(dir, culprit, 64), 0);
}

TEST(ss_stopped_while_loading)
{
    wchar_t dir[MAX_PATH], culprit[64];
    fresh_plugins(dir);
    ss_begin(dir, culprit, 64);
    ss_loading(L"Bad");
    CHECK_STR(marker(dir), "loading Bad\r\n");
    CHECK_EQ(ss_begin(dir, culprit, 64), 1);
    CHECK(wcscmp(culprit, L"Bad") == 0);
    CHECK_STR(marker(dir), "starting\r\n");
}

TEST(ss_stopped_after_loading)
{
    wchar_t dir[MAX_PATH], culprit[64];
    fresh_plugins(dir);
    ss_begin(dir, culprit, 64);
    ss_loading(L"A");
    ss_loaded();
    CHECK_STR(marker(dir), "loaded\r\n");
    CHECK_EQ(ss_begin(dir, culprit, 64), 1);
    CHECK(culprit[0] == 0);
}

TEST(ss_non_ascii_id)
{
    wchar_t dir[MAX_PATH], culprit[64];
    fresh_plugins(dir);
    ss_begin(dir, culprit, 64);
    ss_loading(L"Plug-üж");
    CHECK_EQ(ss_begin(dir, culprit, 64), 1);
    CHECK(wcscmp(culprit, L"Plug-üж") == 0);
}

TEST(ss_safe_start_twice)
{
    wchar_t dir[MAX_PATH], culprit[64];
    fresh_plugins(dir);
    ss_begin(dir, culprit, 64);
    ss_loading(L"Bad");
    CHECK_EQ(ss_begin(dir, culprit, 64), 1);  /* the safe start, which stops too */
    CHECK_EQ(ss_begin(dir, culprit, 64), 1);
    CHECK(culprit[0] == 0);
}

TEST(ss_unwritable_dir_is_normal)
{
    wchar_t dir[MAX_PATH], base[MAX_PATH], culprit[64];
    GetTempPathW(MAX_PATH, base);
    swprintf(dir, MAX_PATH, L"%lsron_ss_missing_%lu\\plugins", base, GetCurrentProcessId());
    CHECK_EQ(ss_begin(dir, culprit, 64), 0);
    ss_loading(L"A");
    ss_loaded();
    ss_menu_reached();
    CHECK(!marker_exists(dir));
    CHECK(GetFileAttributesW(dir) == INVALID_FILE_ATTRIBUTES);
}

/* Final review: the marker is cleared once the menu has drawn a frame when the panel's redraw hook
   took, else at post_init when that hook took, else at once (a start must never stay "unfinished"
   for good). Without game hooks (tests, tools) post_init is fired by hand. */
TEST(ss_clear_point_choices)
{
    CHECK_EQ(ss_clear_point(0, 0, 0), SS_CLEAR_AT_POST_INIT);
    CHECK_EQ(ss_clear_point(0, 1, 1), SS_CLEAR_AT_POST_INIT);
    CHECK_EQ(ss_clear_point(1, 1, 1), SS_CLEAR_AT_FRAME);
    CHECK_EQ(ss_clear_point(1, 1, 0), SS_CLEAR_AT_FRAME);
    CHECK_EQ(ss_clear_point(1, 0, 1), SS_CLEAR_AT_POST_INIT);
    CHECK_EQ(ss_clear_point(1, 0, 0), SS_CLEAR_NOW);
}
