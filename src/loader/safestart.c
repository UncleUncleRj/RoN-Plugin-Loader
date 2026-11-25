#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "loader/safestart.h"

static wchar_t g_path[MAX_PATH];  /* the marker; empty when ss_begin could not use it */

/* Replaces the marker with one line. 0 when it cannot be written. */
static int write_marker(const char *text)
{
    if (!g_path[0])
        return 0;
    FILE *f = _wfopen(g_path, L"wb");
    if (!f)
        return 0;
    size_t n = strlen(text);
    int ok = fwrite(text, 1, n, f) == n && fwrite("\r\n", 1, 2, f) == 2;
    return fclose(f) == 0 && ok;
}

int ss_begin(const wchar_t *plugins_dir, wchar_t *culprit, size_t cap)
{
    if (cap)
        culprit[0] = 0;
    if (_snwprintf(g_path, MAX_PATH, L"%ls\\.start-marker", plugins_dir) < 0) {
        g_path[0] = 0;
        return 0;
    }
    g_path[MAX_PATH - 1] = 0;
    int stopped = 0;
    FILE *f = _wfopen(g_path, L"rb");
    if (f) {
        char text[512];
        size_t n = fread(text, 1, sizeof text - 1, f);
        fclose(f);
        text[n] = 0;
        stopped = 1;
        text[strcspn(text, "\r\n")] = 0;
        if (strncmp(text, "loading ", 8) == 0 && cap) {
            int w = MultiByteToWideChar(CP_UTF8, 0, text + 8, -1, culprit, (int)cap);
            if (w <= 0)
                culprit[0] = 0;
        }
    }
    if (!write_marker("starting")) {
        g_path[0] = 0;
        return 0;
    }
    return stopped;
}

void ss_loading(const wchar_t *id)
{
    char text[8 + 256] = "loading ";
    if (WideCharToMultiByte(CP_UTF8, 0, id, -1, text + 8, 256, NULL, NULL) <= 0)
        text[8] = 0;
    write_marker(text);
}

void ss_loaded(void) { write_marker("loaded"); }

void ss_menu_reached(void)
{
    if (g_path[0])
        DeleteFileW(g_path);
}
