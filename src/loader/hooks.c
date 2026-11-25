#include <windows.h>
#include <string.h>
#include "loader/hooks.h"

#define MAX_RECORDS 1024
#define MAX_LINKS 16
#define MAX_BYTES 64

typedef enum { K_INLINE, K_CALL, K_PTR, K_PATCH } Kind;

typedef struct Link {
    int owner;
    void *detour;
} Link;

typedef struct Record {
    int used;
    Kind kind;
    uintptr_t at;
    uint32_t len;              /* bytes this record changes: stolen, 5, 4 or the patch length */
    uint32_t stock_len;        /* bytes saved in stock (>= len; inline keeps the whole signature span) */
    uint8_t stock[MAX_BYTES];  /* the bytes before anyone changed them */
    int creator;
    void *base;                /* what the first link's *orig is: trampoline, callee or old pointer */
    Link links[MAX_LINKS];
    int nlinks;
} Record;

static Record g_rec[MAX_RECORDS];
static int g_locked;
static HmNotify g_notify;
static uint8_t *g_pool, *g_pool_end;

void hm_set_notify(HmNotify fn) { g_notify = fn; }
void hm_lock(void) { g_locked = 1; }

static void notify(int owner, int other, uintptr_t at, int kind)
{
    if (g_notify)
        g_notify(owner, other, at, kind);
}

/* Committed, readable memory over the whole range (a plugin's wrong address fails that one request). */
static int readable(uintptr_t at, uint32_t n)
{
    uintptr_t p = at, end = at + n;
    if (at < 0x10000 || end < at)
        return 0;
    while (p < end) {
        MEMORY_BASIC_INFORMATION mi;
        if (VirtualQuery((const void *)p, &mi, sizeof mi) != sizeof mi || mi.State != MEM_COMMIT ||
            (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) || mi.Protect == 0)
            return 0;
        p = (uintptr_t)mi.BaseAddress + mi.RegionSize;
    }
    return 1;
}

static int write_mem(uintptr_t at, const void *data, uint32_t n)
{
    DWORD old;
    if (!VirtualProtect((void *)at, n, PAGE_EXECUTE_READWRITE, &old))
        return 0;
    memcpy((void *)at, data, n);
    VirtualProtect((void *)at, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)at, n);
    return 1;
}

static int write_jmp(uintptr_t at, void *to)
{
    uint8_t j[5];
    j[0] = 0xE9;
    int32_t rel = (int32_t)((uintptr_t)to - (at + 5));
    memcpy(j + 1, &rel, 4);
    return write_mem(at, j, 5);
}

static int write_rel32(uintptr_t site, void *to)
{
    int32_t rel = (int32_t)((uintptr_t)to - (site + 5));
    return write_mem(site + 1, &rel, 4);
}

static uint8_t *pool_alloc(uint32_t n)
{
    n = (n + 15) & ~15u;
    if (!g_pool || g_pool + n > g_pool_end) {
        g_pool = VirtualAlloc(NULL, 65536, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!g_pool)
            return NULL;
        g_pool_end = g_pool + 65536;
    }
    uint8_t *p = g_pool;
    g_pool += n;
    return p;
}

static Record *find_at(uintptr_t at)
{
    for (int i = 0; i < MAX_RECORDS; i++) {
        if (g_rec[i].used && g_rec[i].at == at)
            return &g_rec[i];
    }
    return NULL;
}

/* A record whose changed bytes overlap [at, at+len), other than `except`. */
static Record *find_overlap(uintptr_t at, uint32_t len, const Record *except)
{
    for (int i = 0; i < MAX_RECORDS; i++) {
        Record *r = &g_rec[i];
        if (r->used && r != except && at < r->at + r->len && r->at < at + len)
            return r;
    }
    return NULL;
}

static int last_owner(const Record *r)
{
    return r->nlinks ? r->links[r->nlinks - 1].owner : r->creator;
}

static Record *new_record(void)
{
    for (int i = 0; i < MAX_RECORDS; i++) {
        if (!g_rec[i].used) {
            memset(&g_rec[i], 0, sizeof g_rec[i]);
            g_rec[i].used = 1;
            return &g_rec[i];
        }
    }
    return NULL;
}

static int sig_matches(const uint8_t *bytes, const uint8_t *sig, const uint8_t *mask, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        if ((!mask || mask[i]) && bytes[i] != sig[i])
            return 0;
    }
    return 1;
}

static void *head(const Record *r)
{
    return r->nlinks ? r->links[r->nlinks - 1].detour : r->base;
}

/* Points the patched site at the current head of its chain. */
static int apply_head(Record *r)
{
    void *to = head(r);
    if (r->kind == K_INLINE)
        return write_jmp(r->at, to);
    if (r->kind == K_CALL)
        return write_rel32(r->at, to);
    if (r->kind == K_PTR)
        return write_mem(r->at, &to, 4);
    return 1;
}

static RonResult add_link(Record *r, int owner, void *detour, void **orig)
{
    if (r->nlinks >= MAX_LINKS)
        return RON_E_MEM;
    if (r->nlinks)
        notify(owner, last_owner(r), r->at, HM_SHARED);
    void *prev = head(r);
    r->links[r->nlinks].owner = owner;
    r->links[r->nlinks].detour = detour;
    r->nlinks++;
    *orig = prev;  /* before the site points at the detour, so the detour can call on at once */
    if (!apply_head(r)) {
        r->nlinks--;
        if (r->nlinks == 0)
            r->used = 0;  /* nothing was written: the bytes are still the stock ones */
        return RON_E_MEM;
    }
    return RON_OK;
}

static RonResult conflict(int owner, Record *other, uintptr_t at)
{
    notify(owner, last_owner(other), at, HM_CONFLICT);
    return RON_E_CONFLICT;
}

RonResult hm_inline(int owner, uintptr_t at, const uint8_t *sig, const uint8_t *mask, uint32_t sig_len,
                    uint32_t stolen, void *detour, void **orig)
{
    if (g_locked)
        return RON_E_STATE;
    if (!at || !sig || !detour || !orig || stolen < 5 || stolen > sig_len || sig_len > MAX_BYTES)
        return RON_E_ARG;
    if (!readable(at, sig_len))
        return RON_E_ARG;
    Record *r = find_at(at);
    if (r) {
        if (r->kind != K_INLINE)
            return conflict(owner, r, at);
        if (sig_len > r->stock_len) {
            /* A longer signature than the first hook's: the extra bytes lie past every byte this
               record changed, so memory still holds the game's own; nobody else may have changed them. */
            uint32_t extra = sig_len - r->stock_len;
            Record *o = find_overlap(at + r->stock_len, extra, r);
            if (o)
                return conflict(owner, o, at);
            uint8_t full[MAX_BYTES];
            memcpy(full, r->stock, r->stock_len);
            memcpy(full + r->stock_len, (const void *)(at + r->stock_len), extra);
            if (!sig_matches(full, sig, mask, sig_len))
                return RON_E_SIG;
            memcpy(r->stock, full, sig_len);
            r->stock_len = sig_len;
        } else if (!sig_matches(r->stock, sig, mask, sig_len)) {
            return RON_E_SIG;
        }
        return add_link(r, owner, detour, orig);
    }
    Record *o = find_overlap(at, stolen, NULL);
    if (o)
        return conflict(owner, o, at);
    if (!sig_matches((const uint8_t *)at, sig, mask, sig_len))
        return RON_E_SIG;
    uint8_t *tramp = pool_alloc(stolen + 5);
    r = tramp ? new_record() : NULL;
    if (!r)
        return RON_E_MEM;
    /* trampoline: the stolen bytes, then a jmp back to the rest of the function */
    memcpy(tramp, (const void *)at, stolen);
    int32_t back = (int32_t)((at + stolen) - ((uintptr_t)tramp + stolen + 5));
    tramp[stolen] = 0xE9;
    memcpy(tramp + stolen + 1, &back, 4);
    FlushInstructionCache(GetCurrentProcess(), tramp, stolen + 5);
    r->kind = K_INLINE;
    r->at = at;
    r->len = stolen;
    r->stock_len = sig_len;
    memcpy(r->stock, (const void *)at, sig_len);
    r->creator = owner;
    r->base = tramp;
    return add_link(r, owner, detour, orig);
}

RonResult hm_call(int owner, uintptr_t site, void *detour, void **orig)
{
    if (g_locked)
        return RON_E_STATE;
    if (!site || !detour || !orig || !readable(site, 5))
        return RON_E_ARG;
    Record *r = find_at(site);
    if (r) {
        if (r->kind != K_CALL)
            return conflict(owner, r, site);
        return add_link(r, owner, detour, orig);
    }
    Record *o = find_overlap(site, 5, NULL);
    if (o)
        return conflict(owner, o, site);
    if (*(const uint8_t *)site != 0xE8)
        return RON_E_SIG;
    r = new_record();
    if (!r)
        return RON_E_MEM;
    int32_t rel;
    memcpy(&rel, (const void *)(site + 1), 4);
    r->kind = K_CALL;
    r->at = site;
    r->len = r->stock_len = 5;
    memcpy(r->stock, (const void *)site, 5);
    r->creator = owner;
    r->base = (void *)(site + 5 + rel);
    return add_link(r, owner, detour, orig);
}

RonResult hm_ptr(int owner, uintptr_t slot, uintptr_t expected, void *detour, void **orig)
{
    if (g_locked)
        return RON_E_STATE;
    if (!slot || !detour || !orig || !readable(slot, 4))
        return RON_E_ARG;
    Record *r = find_at(slot);
    if (r) {
        if (r->kind != K_PTR)
            return conflict(owner, r, slot);
        if (expected && (uintptr_t)r->base != expected)
            return RON_E_SIG;
        return add_link(r, owner, detour, orig);
    }
    Record *o = find_overlap(slot, 4, NULL);
    if (o)
        return conflict(owner, o, slot);
    uintptr_t now = *(const uintptr_t *)slot;
    if (expected && now != expected)
        return RON_E_SIG;
    r = new_record();
    if (!r)
        return RON_E_MEM;
    r->kind = K_PTR;
    r->at = slot;
    r->len = r->stock_len = 4;
    memcpy(r->stock, (const void *)slot, 4);
    r->creator = owner;
    r->base = (void *)now;
    return add_link(r, owner, detour, orig);
}

RonResult hm_patch(int owner, uintptr_t at, const uint8_t *expect, const uint8_t *bytes, uint32_t len)
{
    if (g_locked)
        return RON_E_STATE;
    if (!at || !expect || !bytes || len == 0 || len > MAX_BYTES || !readable(at, len))
        return RON_E_ARG;
    Record *o = find_overlap(at, len, NULL);
    if (o)
        return conflict(owner, o, at);
    if (memcmp((const void *)at, expect, len) != 0)
        return RON_E_SIG;
    Record *r = new_record();
    if (!r)
        return RON_E_MEM;
    r->kind = K_PATCH;
    r->at = at;
    r->len = r->stock_len = len;
    memcpy(r->stock, (const void *)at, len);
    r->creator = owner;
    if (!write_mem(at, bytes, len)) {
        r->used = 0;
        return RON_E_MEM;
    }
    return RON_OK;
}

void hm_rollback_owner(int owner)
{
    for (int i = MAX_RECORDS - 1; i >= 0; i--) {
        Record *r = &g_rec[i];
        if (!r->used)
            continue;
        if (r->kind == K_PATCH) {
            if (r->creator == owner) {
                write_mem(r->at, r->stock, r->len);
                r->used = 0;
            }
            continue;
        }
        while (r->nlinks && r->links[r->nlinks - 1].owner == owner)
            r->nlinks--;
        if (r->nlinks == 0) {
            write_mem(r->at, r->stock, r->len);  /* back to the game's own bytes */
            r->used = 0;
        } else {
            apply_head(r);
        }
    }
}

void hm_reset(void)
{
    int max_owner = -1;
    for (int i = 0; i < MAX_RECORDS; i++) {
        Record *r = &g_rec[i];
        if (!r->used)
            continue;
        if (r->creator > max_owner)
            max_owner = r->creator;
        for (int j = 0; j < r->nlinks; j++) {
            if (r->links[j].owner > max_owner)
                max_owner = r->links[j].owner;
        }
    }
    for (int o = max_owner; o >= 0; o--)
        hm_rollback_owner(o);
    g_locked = 0;
}
