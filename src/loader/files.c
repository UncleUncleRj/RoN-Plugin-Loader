/* The file service's registry: who serves which game file (design section 9). See files.h. */
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include "loader/files.h"

typedef struct Claim {
    int owner;
    unsigned seq;
    const wchar_t *path;        /* use: the file */
    int check;                  /* use: served only while the file exists (file_use; folder files are walked) */
    RonFileGenFn fn;            /* generate */
    void *user;
    const wchar_t *owner_id;
    int gen_done, gen_ok;
    wchar_t *gen_out;
    struct Claim *next;         /* ascending owner, then seq */
} Claim;

typedef struct Entry {
    wchar_t *rel;               /* as first registered, case kept */
    Claim *claims;
} Entry;

typedef struct Root {
    int owner;
    unsigned seq;
    const wchar_t *root;
    struct Root *next;          /* ascending owner, then seq */
} Root;

typedef struct Slot {
    wchar_t *key;               /* lower case */
    uint32_t h;
    void *val;
} Slot;

typedef struct Table {
    Slot *s;
    uint32_t cap, n;
} Table;

static CRITICAL_SECTION g_cs;
static int g_cs_ready;
static Table g_files, g_dirs;
static wchar_t g_game[MAX_PATH], g_game_slash[MAX_PATH + 1], g_gen[MAX_PATH];
static int g_game_len;
static volatile LONG g_active;
static int g_locked;
static unsigned g_seq;

/* ---- small helpers ---- */

static wchar_t *dupw(const wchar_t *s)
{
    size_t n = wcslen(s) + 1;
    wchar_t *d = malloc(n * sizeof(wchar_t));
    if (d)
        memcpy(d, s, n * sizeof(wchar_t));
    return d;
}

static void lower(wchar_t *s) { CharLowerBuffW(s, (DWORD)wcslen(s)); }

static uint32_t hash_w(const wchar_t *k)
{
    uint32_t h = 2166136261u;
    for (; *k; k++)
        h = (h ^ (uint32_t)*k) * 16777619u;
    return h ? h : 1;
}

static Slot *slot_for(Table *t, const wchar_t *key, uint32_t h)
{
    if (!t->cap)
        return NULL;
    for (uint32_t i = 0; i < t->cap; i++) {
        Slot *s = &t->s[(h + i) & (t->cap - 1)];
        if (!s->key || (s->h == h && wcscmp(s->key, key) == 0))
            return s;
    }
    return NULL;
}

static void *tget(Table *t, const wchar_t *key)
{
    Slot *s = slot_for(t, key, hash_w(key));
    return s && s->key ? s->val : NULL;
}

static int grow(Table *t)
{
    uint32_t cap = t->cap ? t->cap * 2 : 256;
    Slot *s = calloc(cap, sizeof(Slot));
    if (!s)
        return 0;
    Table nt = {s, cap, 0};
    for (uint32_t i = 0; i < t->cap; i++) {
        if (t->s[i].key) {
            Slot *d = slot_for(&nt, t->s[i].key, t->s[i].h);
            *d = t->s[i];
            nt.n++;
        }
    }
    free(t->s);
    *t = nt;
    return 1;
}

/* The value stored under key, made by mk() when absent; NULL when out of memory. */
static void *tget_or_add(Table *t, const wchar_t *key, void *(*mk)(const wchar_t *orig), const wchar_t *orig)
{
    if ((t->n + 1) * 2 > t->cap && !grow(t))
        return NULL;
    uint32_t h = hash_w(key);
    Slot *s = slot_for(t, key, h);
    if (!s)
        return NULL;
    if (s->key)
        return s->val;
    void *v = mk(orig);
    wchar_t *k = v ? dupw(key) : NULL;
    if (!k)
        return NULL;
    s->key = k;
    s->h = h;
    s->val = v;
    t->n++;
    return v;
}

static void *mk_entry(const wchar_t *rel)
{
    Entry *e = calloc(1, sizeof(Entry));
    if (e && !(e->rel = dupw(rel))) {
        free(e);
        return NULL;
    }
    return e;
}

static void *mk_dir(const wchar_t *rel)
{
    (void)rel;
    Root **head = calloc(1, sizeof(Root *));
    return head;
}

static void lock(void)
{
    if (!g_cs_ready) {
        InitializeCriticalSection(&g_cs);
        g_cs_ready = 1;
    }
    EnterCriticalSection(&g_cs);
}

static void unlock(void) { LeaveCriticalSection(&g_cs); }

/* ---- names ---- */

static int ieq(const wchar_t *a, size_t n, const wchar_t *b)
{
    return wcslen(b) == n && _wcsnicmp(a, b, n) == 0;
}

int fs_norm(const wchar_t *name, wchar_t *rel, int cap)
{
    static const wchar_t *const banned[] = {L"dll", L"exe", L"asi", L"sys", L"ocx", L"cpl", L"drv", L"scr", L"com"};
    if (!name || cap < 2)
        return 0;
    const wchar_t *p = name;
    while (p[0] == L'.' && (p[1] == L'\\' || p[1] == L'/'))
        p += 2;
    if (!*p || *p == L'\\' || *p == L'/' || wcschr(p, L':'))
        return 0;
    int n = 0;
    wchar_t prev = 0;
    for (; *p; p++) {
        wchar_t c = *p == L'/' ? L'\\' : *p;
        if (c == L'\\' && prev == L'\\')
            continue;
        if (c < 32 || c == L'*' || c == L'?' || c == L'"' || c == L'<' || c == L'>' || c == L'|')
            return 0;
        if (n + 1 >= cap)
            return 0;
        rel[n++] = c;
        prev = c;
    }
    if (n && rel[n - 1] == L'\\')
        n--;
    rel[n] = 0;
    if (!n)
        return 0;
    const wchar_t *c = rel, *last = rel;
    for (int first = 1;; first = 0) {
        const wchar_t *e = wcschr(c, L'\\');
        size_t len = e ? (size_t)(e - c) : wcslen(c);
        if (ieq(c, len, L".") || ieq(c, len, L"..") || len == 0)
            return 0;
        if (first && (ieq(c, len, L"plugins") || ieq(c, len, L"mods")))
            return 0;
        last = c;
        if (!e)
            break;
        c = e + 1;
    }
    const wchar_t *dot = wcsrchr(last, L'.');
    if (dot) {
        for (size_t i = 0; i < sizeof banned / sizeof banned[0]; i++) {
            if (_wcsicmp(dot + 1, banned[i]) == 0)
                return 0;
        }
    }
    return 1;
}

int fs_rel_of_path(const wchar_t *path, wchar_t *rel, int cap)
{
    if (!path || !g_game_len)
        return 0;
    if (wcsncmp(path, L"\\\\?\\", 4) == 0)
        path += 4;
    if (wcsncmp(path, L"\\\\", 2) == 0 || _wcsnicmp(path, L"UNC\\", 4) == 0)
        return 0;  /* devices, pipes, network paths */
    wchar_t full[MAX_PATH];
    DWORD n = GetFullPathNameW(path, MAX_PATH, full, NULL);
    if (n == 0 || n >= MAX_PATH || _wcsnicmp(full, g_game_slash, (size_t)g_game_len) != 0)
        return 0;
    return fs_norm(full + g_game_len, rel, cap);
}

/* ---- registration ---- */

/* Deletes everything inside dir, and dir itself (no shell32: the loader runs at process start). */
static void rm_tree(const wchar_t *dir, int depth)
{
    if (depth > 24)
        return;
    wchar_t pat[MAX_PATH];
    if (_snwprintf(pat, MAX_PATH, L"%ls\\*", dir) < 0)
        return;
    WIN32_FIND_DATAW d;
    HANDLE h = FindFirstFileW(pat, &d);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(d.cFileName, L".") == 0 || wcscmp(d.cFileName, L"..") == 0)
                continue;
            wchar_t p[MAX_PATH];
            if (_snwprintf(p, MAX_PATH, L"%ls\\%ls", dir, d.cFileName) < 0)
                continue;
            if ((d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(d.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                rm_tree(p, depth + 1);
            } else {
                SetFileAttributesW(p, FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(p);
            }
        } while (FindNextFileW(h, &d));
        FindClose(h);
    }
    RemoveDirectoryW(dir);
}

void fs_init(const wchar_t *game_dir, const wchar_t *gen_dir)
{
    fs_reset();
    wcsncpy(g_game, game_dir ? game_dir : L".", MAX_PATH - 2);
    g_game[MAX_PATH - 2] = 0;
    _snwprintf(g_game_slash, MAX_PATH, L"%ls\\", g_game);
    g_game_len = (int)wcslen(g_game_slash);
    g_gen[0] = 0;
    if (gen_dir) {
        wcsncpy(g_gen, gen_dir, MAX_PATH - 1);
        g_gen[MAX_PATH - 1] = 0;
        /* emptied at every start, so a changed plugin never leaves a stale file (only ever a folder
           inside a .cache folder: a wrong path must not empty anything else) */
        if (wcsstr(g_gen, L"\\.cache\\"))
            rm_tree(g_gen, 0);
    }
}

void fs_reset(void)
{
    lock();
    for (uint32_t i = 0; i < g_files.cap; i++) {
        Slot *s = &g_files.s[i];
        if (!s->key)
            continue;
        Entry *e = s->val;
        for (Claim *c = e->claims, *nx; c; c = nx) {
            nx = c->next;
            free(c->gen_out);
            free(c);
        }
        free(e->rel);
        free(e);
        free(s->key);
    }
    for (uint32_t i = 0; i < g_dirs.cap; i++) {
        Slot *s = &g_dirs.s[i];
        if (!s->key)
            continue;
        Root **head = s->val;
        for (Root *r = *head, *nx; r; r = nx) {
            nx = r->next;
            free(r);
        }
        free(head);
        free(s->key);
    }
    free(g_files.s);
    free(g_dirs.s);
    memset(&g_files, 0, sizeof g_files);
    memset(&g_dirs, 0, sizeof g_dirs);
    g_active = 0;
    g_locked = 0;
    unlock();
}

void fs_lock(void) { g_locked = 1; }

int fs_active(void) { return g_active != 0; }

static void insert_claim(Entry *e, Claim *c)
{
    Claim **pp = &e->claims;
    while (*pp && ((*pp)->owner < c->owner || ((*pp)->owner == c->owner && (*pp)->seq < c->seq)))
        pp = &(*pp)->next;
    c->next = *pp;
    *pp = c;
}

static RonResult add_claim(const wchar_t *rel, Claim *c)
{
    wchar_t key[FS_MAX_REL];
    wcsncpy(key, rel, FS_MAX_REL - 1);
    key[FS_MAX_REL - 1] = 0;
    lower(key);
    Entry *e = tget_or_add(&g_files, key, mk_entry, rel);
    if (!e)
        return RON_E_MEM;
    c->seq = ++g_seq;
    insert_claim(e, c);
    InterlockedExchange(&g_active, 1);
    return RON_OK;
}

RonResult fs_use(int owner, const wchar_t *rel_name, const wchar_t *path)
{
    wchar_t rel[FS_MAX_REL];
    if (!path || !*path || !fs_norm(rel_name, rel, FS_MAX_REL))
        return RON_E_ARG;
    if (g_locked)
        return RON_E_STATE;
    Claim *c = calloc(1, sizeof(Claim));
    if (!c || !(c->path = dupw(path))) {
        free(c);
        return RON_E_MEM;
    }
    c->owner = owner;
    c->check = 1;
    lock();
    RonResult r = add_claim(rel, c);
    unlock();
    return r;
}

RonResult fs_generate(int owner, const wchar_t *owner_id, const wchar_t *rel_name, RonFileGenFn fn, void *user)
{
    wchar_t rel[FS_MAX_REL];
    if (!fn || !fs_norm(rel_name, rel, FS_MAX_REL))
        return RON_E_ARG;
    if (g_locked)
        return RON_E_STATE;
    Claim *c = calloc(1, sizeof(Claim));
    if (!c || !(c->owner_id = dupw(owner_id && *owner_id ? owner_id : L"plugin"))) {
        free(c);
        return RON_E_MEM;
    }
    c->owner = owner;
    c->fn = fn;
    c->user = user;
    lock();
    RonResult r = add_claim(rel, c);
    unlock();
    return r;
}

static RonResult add_dir(int owner, const wchar_t *reldir, const wchar_t *root)
{
    wchar_t key[FS_MAX_REL];
    wcsncpy(key, reldir, FS_MAX_REL - 1);
    key[FS_MAX_REL - 1] = 0;
    lower(key);
    Root **head = tget_or_add(&g_dirs, key, mk_dir, reldir);
    if (!head)
        return RON_E_MEM;
    for (Root *r = *head; r; r = r->next) {
        if (r->owner == owner && r->root == root)
            return RON_OK;
    }
    Root *n = calloc(1, sizeof(Root));
    if (!n)
        return RON_E_MEM;
    n->owner = owner;
    n->seq = ++g_seq;
    n->root = root;
    Root **pp = head;
    while (*pp && ((*pp)->owner < owner || ((*pp)->owner == owner && (*pp)->seq < n->seq)))
        pp = &(*pp)->next;
    n->next = *pp;
    *pp = n;
    InterlockedExchange(&g_active, 1);
    return RON_OK;
}

/* Walks root\sub (sub "" first), serving each file; returns RON_OK or the first error. */
static RonResult walk(int owner, const wchar_t *root, const wchar_t *sub, int depth, int *count)
{
    if (depth > 24)
        return RON_OK;
    wchar_t pat[MAX_PATH];
    if (_snwprintf(pat, MAX_PATH, sub[0] ? L"%ls\\%ls\\*" : L"%ls%ls\\*", root, sub) < 0)
        return RON_OK;
    WIN32_FIND_DATAW d;
    HANDLE h = FindFirstFileExW(pat, FindExInfoBasic, &d, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE)
        return RON_OK;
    RonResult res = RON_OK;
    do {
        if (d.cFileName[0] == L'.' || (d.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)))
            continue;  /* ".", "..", dot folders (.git) and hidden files are never served */
        wchar_t child[FS_MAX_REL], rel[FS_MAX_REL];
        if (_snwprintf(child, FS_MAX_REL, sub[0] ? L"%ls\\%ls" : L"%ls%ls", sub, d.cFileName) < 0 ||
            !fs_norm(child, rel, FS_MAX_REL))
            continue;
        if (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            RonResult r = add_dir(owner, rel, root);
            if (r == RON_OK)
                r = walk(owner, root, child, depth + 1, count);
            if (r != RON_OK && res == RON_OK)
                res = r;
            continue;
        }
        wchar_t full[MAX_PATH];
        if (_snwprintf(full, MAX_PATH, L"%ls\\%ls", root, child) < 0)
            continue;
        Claim *c = calloc(1, sizeof(Claim));
        if (!c || !(c->path = dupw(full))) {
            free(c);
            res = RON_E_MEM;
            continue;
        }
        c->owner = owner;
        if (add_claim(rel, c) == RON_OK)
            ++*count;
        else
            res = RON_E_MEM;
    } while (FindNextFileW(h, &d));
    FindClose(h);
    return res;
}

RonResult fs_folder(int owner, const wchar_t *folder, int *count)
{
    int n = 0;
    if (count)
        *count = 0;
    if (!folder || !*folder)
        return RON_E_ARG;
    DWORD a = GetFileAttributesW(folder);
    if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY))
        return RON_E_ARG;
    if (g_locked)
        return RON_E_STATE;
    wchar_t *root = dupw(folder);
    if (!root)
        return RON_E_MEM;
    size_t len = wcslen(root);
    while (len > 3 && (root[len - 1] == L'\\' || root[len - 1] == L'/'))
        root[--len] = 0;
    lock();
    RonResult r = walk(owner, root, L"", 0, &n);
    unlock();
    if (count)
        *count = n;
    return r;
}

void fs_drop_owner(int owner)
{
    /* Unlinked, never freed: the game may hold a path handed out while the plugin was loading. */
    lock();
    for (uint32_t i = 0; i < g_files.cap; i++) {
        if (!g_files.s[i].key)
            continue;
        Entry *e = g_files.s[i].val;
        for (Claim **pp = &e->claims; *pp;) {
            if ((*pp)->owner == owner)
                *pp = (*pp)->next;
            else
                pp = &(*pp)->next;
        }
    }
    for (uint32_t i = 0; i < g_dirs.cap; i++) {
        if (!g_dirs.s[i].key)
            continue;
        for (Root **pp = g_dirs.s[i].val; *pp;) {
            if ((*pp)->owner == owner)
                *pp = (*pp)->next;
            else
                pp = &(*pp)->next;
        }
    }
    unlock();
}

/* ---- lookups ---- */

static int make_dirs_for(const wchar_t *file)
{
    wchar_t p[MAX_PATH];
    wcsncpy(p, file, MAX_PATH - 1);
    p[MAX_PATH - 1] = 0;
    for (wchar_t *s = p + 3; *s; s++) {
        if (*s == L'\\') {
            *s = 0;
            CreateDirectoryW(p, NULL);
            *s = L'\\';
        }
    }
    return 1;
}

static int exists_file(const wchar_t *p)
{
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static const wchar_t *run_generator(Claim *c, const wchar_t *rel, const wchar_t *in)
{
    if (!c->gen_done) {
        c->gen_done = 1;
        wchar_t out[MAX_PATH];
        char rel8[FS_MAX_REL * 3];
        if (g_gen[0] && _snwprintf(out, MAX_PATH, L"%ls\\%ls\\%ls", g_gen, c->owner_id, rel) > 0 &&
            WideCharToMultiByte(CP_UTF8, 0, rel, -1, rel8, sizeof rel8, NULL, NULL) > 0) {
            make_dirs_for(out);
            DeleteFileW(out);
            if (c->fn(rel8, in, out, c->user) && exists_file(out)) {
                c->gen_out = dupw(out);
                c->gen_ok = c->gen_out != NULL;
            }
        }
    }
    return c->gen_ok ? c->gen_out : NULL;
}

const wchar_t *fs_resolve(const wchar_t *rel, const wchar_t *base)
{
    if (!g_active || !rel || !*rel)
        return NULL;
    wchar_t key[FS_MAX_REL];
    wcsncpy(key, rel, FS_MAX_REL - 1);
    key[FS_MAX_REL - 1] = 0;
    lower(key);
    lock();
    Entry *e = tget(&g_files, key);
    const wchar_t *served = NULL;
    if (e && e->claims) {
        wchar_t in[MAX_PATH];
        if (!base || !*base)
            _snwprintf(in, MAX_PATH, L"%ls\\%ls", g_game, rel);
        else if (base[0] == L'\\' || wcschr(base, L':'))
            _snwprintf(in, MAX_PATH, L"%ls", base);
        else
            _snwprintf(in, MAX_PATH, L"%ls\\%ls", g_game, base);
        in[MAX_PATH - 1] = 0;
        const wchar_t *cur = in;
        for (Claim *c = e->claims; c; c = c->next) {
            if (c->fn) {
                const wchar_t *g = run_generator(c, e->rel, cur);
                if (g)
                    cur = served = g;
            } else if (!c->check || exists_file(c->path)) {
                cur = served = c->path;
            }
        }
    }
    unlock();
    return served;
}

int fs_dir_roots(const wchar_t *reldir, const wchar_t **roots, int max)
{
    if (!g_active || !reldir || !*reldir)
        return 0;
    wchar_t key[FS_MAX_REL];
    wcsncpy(key, reldir, FS_MAX_REL - 1);
    key[FS_MAX_REL - 1] = 0;
    lower(key);
    int n = 0;
    lock();
    Root **head = tget(&g_dirs, key);
    for (Root *r = head ? *head : NULL; r && n < max; r = r->next)
        roots[n++] = r->root;
    unlock();
    return n;
}

int fs_owner_files(int owner)
{
    int n = 0;
    lock();
    for (uint32_t i = 0; i < g_files.cap; i++) {
        if (!g_files.s[i].key)
            continue;
        for (Claim *c = ((Entry *)g_files.s[i].val)->claims; c; c = c->next) {
            if (c->owner == owner) {
                n++;
                break;
            }
        }
    }
    unlock();
    return n;
}
