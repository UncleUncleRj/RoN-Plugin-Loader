#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "check.h"
#include "loader/log.h"

TEST(log_writes_lines)
{
    wchar_t path[MAX_PATH];
    GetTempPathW(MAX_PATH, path);
    wcscat(path, L"ron_loader_test_log.txt");
    CHECK(log_open(path));
    log_line("loader", "x=%d", 5);
    log_close();

    FILE *f = _wfopen(path, L"rb");
    CHECK(f != NULL);
    if (!f)
        return;
    char buf[256] = {0};
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    DeleteFileW(path);

    const char *want = "[loader] x=5\r\n";
    size_t wl = strlen(want);
    CHECK(n >= wl);
    CHECK(n >= wl && memcmp(buf + n - wl, want, wl) == 0);
    int lines = 0;
    for (size_t i = 0; i < n; i++)
        lines += buf[i] == '\n';
    CHECK_EQ(lines, 1);
    CHECK_EQ(n, 13 + wl);  /* "HH:MM:SS.mmm " */
}
