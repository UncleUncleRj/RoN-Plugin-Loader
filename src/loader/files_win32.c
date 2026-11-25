/*
 * The file service's catch-all (design section 9): reads of <game>\<name> anywhere in the process.
 * The game's own lookup (String::prepend_content_dir, files_game.c) serves most files, but some
 * readers open a path without it (MSXML reads the interface files itself, d3dgl its shaders), so
 * KernelBase's CreateFileW, GetFileAttributesW, GetFileAttributesExW and FindFirstFileExW are hooked
 * too (the hooks RON Revived's winmm.dll used for its mod_override folder). Only reads are sent to a
 * plugin's file; a folder listing (FindFirstFileExW with * or ?) gets the plugins' added files and
 * folders merged in, through FindNextFileW and FindClose.
 */
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include "loader/files.h"
#include "loader/files_win32.h"
#include "loader/hooks.h"
#include "loader/log.h"

typedef HANDLE(WINAPI *CreateFn)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef DWORD(WINAPI *AttrFn)(LPCWSTR);
typedef BOOL(WINAPI *AttrExFn)(LPCWSTR, GET_FILEEX_INFO_LEVELS, LPVOID);
typedef HANDLE(WINAPI *FindExFn)(LPCWSTR, FINDEX_INFO_LEVELS, LPVOID, FINDEX_SEARCH_OPS, LPVOID, DWORD);
typedef BOOL(WINAPI *FindNextFn)(HANDLE, LPWIN32_FIND_DATAW);
typedef BOOL(WINAPI *FindCloseFn)(HANDLE);

static CreateFn o_create;
static AttrFn o_attr;
static AttrExFn o_attr_ex;
static FindExFn o_find_ex;
static FindNextFn o_find_next;
static FindCloseFn o_find_close;

static DWORD g_tls = TLS_OUT_OF_INDEXES;  /* set while inside the service: its own file calls pass */

#define WRITE_ACCESS (GENERIC_WRITE | GENERIC_ALL | FILE_WRITE_DATA | FILE_APPEND_DATA | DELETE)
#define MAX_ROOTS 32

static int enter(void)
{
    if (!fs_active() || g_tls == TLS_OUT_OF_INDEXES || TlsGetValue(g_tls))
        return 0;
    TlsSetValue(g_tls, (void *)1);
    return 1;
}

static void leave(void) { TlsSetValue(g_tls, NULL); }

static int exists(const wchar_t *p)
{
    return o_attr ? o_attr(p) != INVALID_FILE_ATTRIBUTES : GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES;
}

/* The path a read of `name` goes to: a plugin's file, or, for a folder that only plugins have, the
   first plugin's folder (dirs != 0). NULL: leave it. */
static const wchar_t *redirect(const wchar_t *name, int dirs, wchar_t *buf)
{
    if (!name || !enter())
        return NULL;
    const wchar_t *r = NULL;
    wchar_t rel[FS_MAX_REL];
    if (fs_rel_of_path(name, rel, FS_MAX_REL)) {
        r = fs_resolve(rel, NULL);
        if (!r && dirs && !exists(name)) {
            const wchar_t *roots[1];
            if (fs_dir_roots(rel, roots, 1) == 1 && _snwprintf(buf, MAX_PATH, L"%ls\\%ls", roots[0], rel) > 0)
                r = buf;
        }
    }
    leave();
    return r;
}

static HANDLE WINAPI d_create(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp,
                              DWORD flags, HANDLE tmpl)
{
    wchar_t buf[MAX_PATH];
    const wchar_t *p = !(access & WRITE_ACCESS) && disp == OPEN_EXISTING ? redirect(name, 0, buf) : NULL;
    return o_create(p ? p : name, access, share, sa, disp, flags, tmpl);
}

static DWORD WINAPI d_attr(LPCWSTR name)
{
    wchar_t buf[MAX_PATH];
    const wchar_t *p = redirect(name, 1, buf);
    return o_attr(p ? p : name);
}

static BOOL WINAPI d_attr_ex(LPCWSTR name, GET_FILEEX_INFO_LEVELS level, LPVOID info)
{
    wchar_t buf[MAX_PATH];
    const wchar_t *p = redirect(name, 1, buf);
    return o_attr_ex(p ? p : name, level, info);
}

/* ---- merged folder listings ---- */

typedef struct Merged {
    int n, i, cap;
    WIN32_FIND_DATAW *e;
} Merged;

static CRITICAL_SECTION g_mcs;
static Merged *g_live[64];  /* the merged listings open now (a handle is one of these or the system's) */

static int is_merged(HANDLE h)
{
    int r = 0;
    EnterCriticalSection(&g_mcs);
    for (int i = 0; i < 64 && !r; i++)
        r = g_live[i] && (HANDLE)g_live[i] == h;
    LeaveCriticalSection(&g_mcs);
    return r;
}

static int track(Merged *m)
{
    int ok = 0;
    EnterCriticalSection(&g_mcs);
    for (int i = 0; i < 64 && !ok; i++) {
        if (!g_live[i]) {
            g_live[i] = m;
            ok = 1;
        }
    }
    LeaveCriticalSection(&g_mcs);
    return ok;
}

static void untrack(Merged *m)
{
    EnterCriticalSection(&g_mcs);
    for (int i = 0; i < 64; i++) {
        if (g_live[i] == m)
            g_live[i] = NULL;
    }
    LeaveCriticalSection(&g_mcs);
}

static void add_unique(Merged *m, const WIN32_FIND_DATAW *d)
{
    for (int i = 0; i < m->n; i++) {
        if (_wcsicmp(m->e[i].cFileName, d->cFileName) == 0)
            return;
    }
    if (m->n == m->cap) {
        int cap = m->cap ? m->cap * 2 : 64;
        WIN32_FIND_DATAW *e = realloc(m->e, (size_t)cap * sizeof *e);
        if (!e)
            return;
        m->e = e;
        m->cap = cap;
    }
    m->e[m->n++] = *d;
}

static void collect(Merged *m, const wchar_t *pattern, FINDEX_INFO_LEVELS level, FINDEX_SEARCH_OPS op, DWORD flags)
{
    WIN32_FIND_DATAW d;
    HANDLE h = o_find_ex(pattern, level, &d, op, NULL, flags);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do
        add_unique(m, &d);
    while (o_find_next(h, &d));
    o_find_close(h);
}

/* A listing of <game>\<dir>\<pattern> where plugins have <dir>: the game's entries, then each
   plugin's that the game lacks. NULL: list as usual. */
static Merged *merged_listing(const wchar_t *name, FINDEX_INFO_LEVELS level, FINDEX_SEARCH_OPS op, DWORD flags)
{
    const wchar_t *slash = wcsrchr(name, L'\\');
    const wchar_t *fwd = wcsrchr(name, L'/');
    if (fwd && (!slash || fwd > slash))
        slash = fwd;
    if (!slash || slash == name || (!wcschr(slash, L'*') && !wcschr(slash, L'?')))
        return NULL;
    wchar_t dir[MAX_PATH], rel[FS_MAX_REL];
    size_t dl = (size_t)(slash - name);
    if (dl >= MAX_PATH)
        return NULL;
    memcpy(dir, name, dl * sizeof(wchar_t));
    dir[dl] = 0;
    if (!fs_rel_of_path(dir, rel, FS_MAX_REL))
        return NULL;  /* outside the game folder, or the game folder itself (never merged) */
    const wchar_t *roots[MAX_ROOTS];
    int nr = fs_dir_roots(rel, roots, MAX_ROOTS);
    if (nr == 0)
        return NULL;
    Merged *m = calloc(1, sizeof(Merged));
    if (!m)
        return NULL;
    collect(m, name, level, op, flags);
    for (int i = nr - 1; i >= 0; i--) {  /* the lowest plugin's entry first, as it wins its files */
        wchar_t pat[MAX_PATH];
        if (_snwprintf(pat, MAX_PATH, L"%ls\\%ls\\%ls", roots[i], rel, slash + 1) > 0)
            collect(m, pat, level, op, flags);
    }
    return m;
}

static HANDLE WINAPI d_find_ex(LPCWSTR name, FINDEX_INFO_LEVELS level, LPVOID data, FINDEX_SEARCH_OPS op,
                               LPVOID filter, DWORD flags)
{
    wchar_t buf[MAX_PATH];
    const wchar_t *p = redirect(name, 1, buf);
    if (p || !name || filter || (level != FindExInfoStandard && level != FindExInfoBasic) || !enter())
        return o_find_ex(p ? p : name, level, data, op, filter, flags);
    Merged *m = merged_listing(name, level, op, flags);
    leave();
    if (!m)
        return o_find_ex(name, level, data, op, filter, flags);
    if (m->n == 0 || !track(m)) {
        free(m->e);
        free(m);
        SetLastError(ERROR_FILE_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    *(WIN32_FIND_DATAW *)data = m->e[0];
    m->i = 1;
    return (HANDLE)m;
}

static BOOL WINAPI d_find_next(HANDLE h, LPWIN32_FIND_DATAW data)
{
    if (!is_merged(h))
        return o_find_next(h, data);
    Merged *m = (Merged *)h;
    if (m->i >= m->n) {
        SetLastError(ERROR_NO_MORE_FILES);
        return FALSE;
    }
    *data = m->e[m->i++];
    return TRUE;
}

static BOOL WINAPI d_find_close(HANDLE h)
{
    if (!is_merged(h))
        return o_find_close(h);
    Merged *m = (Merged *)h;
    untrack(m);
    free(m->e);
    free(m);
    return TRUE;
}

/* ---- install ---- */

/* KernelBase's exports start with the hot-patch prologue: mov edi,edi; push ebp; mov ebp,esp. */
static const uint8_t k_prologue[5] = {0x8B, 0xFF, 0x55, 0x8B, 0xEC};

/* FindClose has none: push 0Ch; push <SEH scope table> (absolute, so masked); 7 bytes, no branch. */
static const uint8_t k_push_seh[7] = {0x6A, 0x0C, 0x68, 0, 0, 0, 0};
static const uint8_t k_push_seh_mask[7] = {1, 1, 1, 0, 0, 0, 0};

static int hook_export(HMODULE kb, const char *name, void *detour, void **orig)
{
    uintptr_t at = (uintptr_t)GetProcAddress(kb, name);
    RonResult r = RON_E_ARG;
    if (at && memcmp((const void *)at, k_prologue, 5) == 0)
        r = hm_inline(0, at, k_prologue, NULL, 5, 5, detour, orig);
    else if (at)
        r = hm_inline(0, at, k_push_seh, k_push_seh_mask, 7, 7, detour, orig);
    if (r != RON_OK)
        log_line("loader", "files: %s not hooked (%d)", name, (int)r);
    return r == RON_OK;
}

static int s_done, s_count;

void fs_win32_forget(void) { s_done = 0; }

int fs_win32_install(void)
{
    if (s_done)
        return s_count;
    s_done = 1;
    static int s_init;
    if (!s_init) {
        s_init = 1;
        InitializeCriticalSection(&g_mcs);
        g_tls = TlsAlloc();
    }
    HMODULE kb = GetModuleHandleW(L"kernelbase.dll");
    if (g_tls == TLS_OUT_OF_INDEXES || !kb)
        return 0;
    /* the listing hooks go first and together: a merged handle must never reach the system's
       FindNextFileW or FindClose */
    int list = hook_export(kb, "FindNextFileW", (void *)d_find_next, (void **)&o_find_next) &&
               hook_export(kb, "FindClose", (void *)d_find_close, (void **)&o_find_close);
    s_count = hook_export(kb, "GetFileAttributesW", (void *)d_attr, (void **)&o_attr) +
              hook_export(kb, "CreateFileW", (void *)d_create, (void **)&o_create) +
              hook_export(kb, "GetFileAttributesExW", (void *)d_attr_ex, (void **)&o_attr_ex);
    if (list)
        s_count += 2 + hook_export(kb, "FindFirstFileExW", (void *)d_find_ex, (void **)&o_find_ex);
    return s_count;
}
