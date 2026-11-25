#include <string.h>
#include "loader/manifest.h"

static int ieq(const char *a, size_t alen, const char *b)
{
    size_t blen = strlen(b);
    if (alen != blen)
        return 0;
    for (size_t i = 0; i < alen; i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z')
            x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z')
            y = (char)(y - 'A' + 'a');
        if (x != y)
            return 0;
    }
    return 1;
}

static void trim(const char **s, size_t *n)
{
    while (*n && (**s == ' ' || **s == '\t')) {
        (*s)++;
        (*n)--;
    }
    while (*n && ((*s)[*n - 1] == ' ' || (*s)[*n - 1] == '\t' || (*s)[*n - 1] == '\r'))
        (*n)--;
}

/* Copies at most cap-1 bytes, never cutting a UTF-8 character in two. Returns 0 when it had to cut. */
static int copy_value(char *dst, size_t cap, const char *v, size_t n)
{
    int whole = 1;
    if (n >= cap) {
        whole = 0;
        n = cap - 1;
        while (n > 0 && ((unsigned char)v[n] & 0xC0) == 0x80)
            n--;
    }
    memcpy(dst, v, n);
    dst[n] = 0;
    return whole;
}

static int folder_ok(const char *v, size_t n)
{
    if ((n == 1 && v[0] == '.') || (n == 2 && v[0] == '.' && v[1] == '.'))
        return 0;
    for (size_t i = 0; i < n; i++) {
        if (v[i] == '\\' || v[i] == '/' || v[i] == ':')
            return 0;
    }
    return 1;
}

int manifest_parse(const char *utf8, size_t len, Manifest *m)
{
    memset(m, 0, sizeof *m);
    m->affects_sim = 1;
    const char *p = utf8, *end = utf8 + len;
    if (len >= 3 && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF)
        p += 3;
    int found = 0, in_plugin = 0;
    while (p < end) {
        const char *eol = memchr(p, '\n', (size_t)(end - p));
        const char *line = p;
        size_t n = (size_t)((eol ? eol : end) - p);
        p = eol ? eol + 1 : end;
        trim(&line, &n);
        if (n == 0 || line[0] == ';' || line[0] == '#')
            continue;
        if (line[0] == '[') {
            const char *close = memchr(line, ']', n);
            if (!close)
                continue;
            const char *name = line + 1;
            size_t nn = (size_t)(close - name);
            trim(&name, &nn);
            in_plugin = ieq(name, nn, "plugin");
            found |= in_plugin;
            continue;
        }
        if (!in_plugin)
            continue;
        const char *eq = memchr(line, '=', n);
        if (!eq)
            continue;
        const char *key = line, *val = eq + 1;
        size_t kn = (size_t)(eq - line), vn = n - kn - 1;
        trim(&key, &kn);
        trim(&val, &vn);
        if (ieq(key, kn, "name"))
            copy_value(m->name, sizeof m->name, val, vn);
        else if (ieq(key, kn, "version"))
            copy_value(m->version, sizeof m->version, val, vn);
        else if (ieq(key, kn, "author"))
            copy_value(m->author, sizeof m->author, val, vn);
        else if (ieq(key, kn, "description"))
            copy_value(m->description, sizeof m->description, val, vn);
        else if (ieq(key, kn, "affectssim"))
            m->affects_sim = !(vn == 1 && val[0] == '0');
        else if (ieq(key, kn, "folder")) {
            m->folder[0] = 0;
            m->folder_bad = 0;
            if (vn == 0)
                continue;
            /* a folder name that doesn't fit is not shortened: that would name another folder */
            if (!folder_ok(val, vn) || vn >= sizeof m->folder)
                m->folder_bad = 1;
            else
                copy_value(m->folder, sizeof m->folder, val, vn);
        }
    }
    return found;
}
