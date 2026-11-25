#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include "loader/order.h"

static const char k_header[] = "# Load order: top loads first; the lowest plugin wins. A leading - turns a plugin off.\r\n";

static int ci_equal(const wchar_t *a, const wchar_t *b)
{
    return CompareStringOrdinal(a, -1, b, -1, TRUE) == CSTR_EQUAL;
}

static int ci_compare(const void *a, const void *b)
{
    return CompareStringOrdinal((const wchar_t *)a, -1, (const wchar_t *)b, -1, TRUE) - CSTR_EQUAL;
}

int order_discover(const wchar_t *plugins_dir, wchar_t (*names)[64], int max, void (*on_skip)(const wchar_t *dll))
{
    wchar_t pattern[MAX_PATH];
    if (_snwprintf(pattern, MAX_PATH, L"%ls\\*.dll", plugins_dir) < 0)
        return 0;
    pattern[MAX_PATH - 1] = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(pattern, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, 0);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    int n = 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        size_t len = wcslen(fd.cFileName);
        /* "*.dll" can also match longer extensions through 8.3 names: check the real one */
        if (len < 5 || CompareStringOrdinal(fd.cFileName + len - 4, -1, L".dll", -1, TRUE) != CSTR_EQUAL)
            continue;
        size_t id_len = len - 4;
        wchar_t ini[MAX_PATH];
        DWORD attr = INVALID_FILE_ATTRIBUTES;
        if (id_len < 64 && _snwprintf(ini, MAX_PATH, L"%ls\\%.*ls.ini", plugins_dir, (int)id_len, fd.cFileName) > 0) {
            ini[MAX_PATH - 1] = 0;
            attr = GetFileAttributesW(ini);
        }
        if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) {
            if (on_skip)
                on_skip(fd.cFileName);
            continue;
        }
        if (n < max) {
            memcpy(names[n], fd.cFileName, id_len * sizeof(wchar_t));
            names[n][id_len] = 0;
            n++;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    qsort(names, (size_t)n, sizeof names[0], ci_compare);
    return n;
}

int order_parse(const char *utf8, size_t len, OrderItem *out, int max)
{
    const char *p = utf8, *end = utf8 + len;
    if (len >= 3 && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF)
        p += 3;
    int n = 0;
    while (p < end) {
        const char *eol = memchr(p, '\n', (size_t)(end - p));
        const char *s = p;
        const char *e = eol ? eol : end;
        p = eol ? eol + 1 : end;
        while (s < e && (*s == ' ' || *s == '\t'))
            s++;
        while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r'))
            e--;
        if (s == e || *s == '#')
            continue;
        int enabled = 1;
        if (*s == '-') {
            enabled = 0;
            s++;
            while (s < e && (*s == ' ' || *s == '\t'))
                s++;
        }
        if (s == e || n >= max)
            continue;
        wchar_t name[64];
        int w = MultiByteToWideChar(CP_UTF8, 0, s, (int)(e - s), name, 63);
        if (w <= 0)
            continue;  /* too long or not UTF-8 */
        name[w] = 0;
        int dup = 0;
        for (int i = 0; i < n && !dup; i++)
            dup = ci_equal(out[i].name, name);
        if (dup)
            continue;
        memcpy(out[n].name, name, sizeof name);
        out[n].enabled = enabled;
        out[n].is_new = 0;
        n++;
    }
    return n;
}

int order_merge(const OrderItem *listed, int n_listed, const wchar_t (*found)[64], int n_found, OrderItem *out,
                int max, int *dropped)
{
    char used[ORDER_MAX] = {0};
    int n = 0, gone = 0;
    for (int i = 0; i < n_listed; i++) {
        int hit = -1;
        for (int j = 0; j < n_found && j < ORDER_MAX && hit < 0; j++) {
            if (!used[j] && ci_equal(listed[i].name, found[j]))
                hit = j;
        }
        if (hit < 0) {
            gone++;
            continue;
        }
        used[hit] = 1;
        if (n < max) {
            memcpy(out[n].name, found[hit], sizeof out[n].name);  /* the spelling on disk */
            out[n].enabled = listed[i].enabled;
            out[n].is_new = 0;
            n++;
        }
    }
    for (int j = 0; j < n_found && j < ORDER_MAX; j++) {
        if (used[j] || n >= max)
            continue;
        memcpy(out[n].name, found[j], sizeof out[n].name);
        out[n].enabled = 1;
        out[n].is_new = 1;
        n++;
    }
    if (dropped)
        *dropped = gone;
    return n;
}

size_t order_serialize(const OrderItem *items, int n, char *buf, size_t cap)
{
    size_t len = sizeof k_header - 1;
    if (cap <= len)
        return 0;
    memcpy(buf, k_header, len);
    for (int i = 0; i < n; i++) {
        if (!items[i].enabled) {
            if (len + 1 >= cap)
                return 0;
            buf[len++] = '-';
        }
        int w = WideCharToMultiByte(CP_UTF8, 0, items[i].name, -1, buf + len, (int)(cap - len), NULL, NULL);
        if (w <= 0)
            return 0;
        len += (size_t)w - 1;  /* without the NUL */
        if (len + 3 > cap)
            return 0;
        buf[len++] = '\r';
        buf[len++] = '\n';
    }
    buf[len] = 0;
    return len;
}
