#include <windows.h>
#include <wchar.h>
#include "check.h"
#include "loader/symbols.h"

/* tests\fixtures\<name>, found from build\tests.exe */
static void fixture(wchar_t *out, const wchar_t *name)
{
    GetModuleFileNameW(NULL, out, MAX_PATH);
    wchar_t *slash = wcsrchr(out, L'\\');
    *slash = 0;
    slash = wcsrchr(out, L'\\');
    *slash = 0;
    wcscat(out, L"\\tests\\fixtures\\");
    wcscat(out, name);
}

TEST(sym_lookup)
{
    wchar_t p[MAX_PATH];
    fixture(p, L"mini.map");
    CHECK_EQ(sym_load(p, 0x6674863F), 3);
    CHECK_EQ(sym_static("?do_frame@Game@@QAEXXZ"), 0x591EF0);
    CHECK_EQ(sym_static("?main@Main@@SAHPAUHINSTANCE__@@0PADH@Z"), 0x560CF0);
    CHECK_EQ(sym_static("?check_result@TopMenu@@QAE?AW4GameIndex@@ABVString@@@Z"), 0x59B1E0);
    sym_free();
}

TEST(sym_first_duplicate_wins)
{
    wchar_t p[MAX_PATH];
    fixture(p, L"mini.map");
    sym_load(p, 0x6674863F);
    CHECK(sym_static("?do_frame@Game@@QAEXXZ") != 0x999999);
    sym_free();
}

TEST(sym_unknown_zero)
{
    wchar_t p[MAX_PATH];
    fixture(p, L"mini.map");
    sym_load(p, 0x6674863F);
    CHECK_EQ(sym_static("?nope@@YAXXZ"), 0);
    CHECK_EQ(sym_static(".text"), 0);
    sym_free();
}

TEST(sym_wrong_timestamp)
{
    wchar_t p[MAX_PATH];
    fixture(p, L"mini.map");
    CHECK_EQ(sym_load(p, 0x66748640), -1);
    CHECK_EQ(sym_static("?do_frame@Game@@QAEXXZ"), 0);
    sym_free();
}

TEST(sym_missing_file)
{
    wchar_t p[MAX_PATH];
    fixture(p, L"no_such.map");
    CHECK_EQ(sym_load(p, 0x6674863F), -1);
    CHECK_EQ(sym_static("?do_frame@Game@@QAEXXZ"), 0);
}
