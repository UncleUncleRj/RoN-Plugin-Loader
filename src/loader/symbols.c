#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include "loader/symbols.h"

typedef struct SymEntry { const char *name; uint32_t va; } SymEntry;

static char *g_text;        /* the whole map; names point into it */
static SymEntry *g_table;   /* open addressing, power-of-two size */
static uint32_t g_mask;

static uint32_t hash_name(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s)
        h = (h ^ (unsigned char)*s++) * 16777619u;
    return h;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static int parse_hex8(const char *s, size_t n, uint32_t *out)
{
    if (n != 8)
        return 0;
    uint32_t v = 0;
    for (size_t i = 0; i < 8; i++) {
        int d = hexval(s[i]);
        if (d < 0)
            return 0;
        v = v << 4 | (uint32_t)d;
    }
    *out = v;
    return 1;
}

static int is_section_offset(const char *s, size_t n)
{
    if (n != 13 || s[4] != ':')
        return 0;
    for (size_t i = 0; i < 13; i++) {
        if (i != 4 && hexval(s[i]) < 0)
            return 0;
    }
    return 1;
}

void sym_free(void)
{
    free(g_text);
    free(g_table);
    g_text = NULL;
    g_table = NULL;
    g_mask = 0;
}

static char *read_all(const wchar_t *path, size_t *len)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return NULL;
    LARGE_INTEGER size;
    char *buf = NULL;
    if (GetFileSizeEx(h, &size) && size.QuadPart < 256 * 1024 * 1024) {
        buf = malloc((size_t)size.QuadPart + 1);
        DWORD got = 0;
        if (buf && ReadFile(h, buf, (DWORD)size.QuadPart, &got, NULL) && got == (DWORD)size.QuadPart) {
            buf[got] = 0;
            *len = got;
        } else {
            free(buf);
            buf = NULL;
        }
    }
    CloseHandle(h);
    return buf;
}

int sym_load(const wchar_t *map_path, uint32_t exe_timestamp)
{
    sym_free();
    size_t len = 0;
    char *text = read_all(map_path, &len);
    if (!text)
        return -1;

    const char *ts = strstr(text, "Timestamp is ");
    uint32_t map_ts = 0;
    if (!ts || ts - text > 4096 || !parse_hex8(ts + 13, 8, &map_ts) || map_ts != exe_timestamp) {
        free(text);
        return -1;
    }

    size_t lines = 1;
    for (size_t i = 0; i < len; i++)
        lines += text[i] == '\n';
    uint32_t cap = 1024;
    while (cap < lines * 2)
        cap <<= 1;
    SymEntry *table = calloc(cap, sizeof *table);
    if (!table) {
        free(text);
        return -1;
    }

    int count = 0;
    char *p = text, *end = text + len;
    while (p < end) {
        char *eol = memchr(p, '\n', (size_t)(end - p));
        char *line_end = eol ? eol : end;
        char *tok[3];
        size_t tlen[3];
        int nt = 0;
        char *s = p;
        while (nt < 3 && s < line_end) {
            while (s < line_end && (*s == ' ' || *s == '\t' || *s == '\r'))
                s++;
            if (s >= line_end)
                break;
            tok[nt] = s;
            while (s < line_end && *s != ' ' && *s != '\t' && *s != '\r')
                s++;
            tlen[nt] = (size_t)(s - tok[nt]);
            nt++;
        }
        p = eol ? eol + 1 : end;
        uint32_t va;
        if (nt < 3 || !is_section_offset(tok[0], tlen[0]) || !parse_hex8(tok[2], tlen[2], &va))
            continue;
        tok[1][tlen[1]] = 0;  /* the byte after the name is a space: end the name there */
        uint32_t i = hash_name(tok[1]) & (cap - 1);
        while (table[i].name && strcmp(table[i].name, tok[1]) != 0)
            i = (i + 1) & (cap - 1);
        if (!table[i].name) {  /* the first entry for a name wins */
            table[i].name = tok[1];
            table[i].va = va;
            count++;
        }
    }
    g_text = text;
    g_table = table;
    g_mask = cap - 1;
    return count;
}

uint32_t sym_static(const char *decorated)
{
    if (!g_table || !decorated)
        return 0;
    uint32_t i = hash_name(decorated) & g_mask;
    while (g_table[i].name) {
        if (strcmp(g_table[i].name, decorated) == 0)
            return g_table[i].va;
        i = (i + 1) & g_mask;
    }
    return 0;
}
