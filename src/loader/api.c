/* RonApi v1: each call is bound to the calling plugin's owner index (its place in the load order). */
#include <stdarg.h>
#include <stdio.h>
#include "loader/loader.h"
#include "loader/log.h"
#include "loader/hooks.h"
#include "loader/events.h"
#include "loader/symbols.h"

static int owner_of(RonPlugin *self) { return self ? self->owner : -1; }

static void api_log(RonPlugin *self, const char *fmt, ...)
{
    char text[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof text, fmt ? fmt : "", ap);
    va_end(ap);
    log_line(loader_owner_name(owner_of(self)), "%s", text);
}

static const RonGameInfo *api_game(void) { return loader_game(); }

static uintptr_t api_va(uintptr_t static_va) { return static_va + loader_game()->delta; }

static uintptr_t api_sym(const char *decorated_name)
{
    uint32_t va = sym_static(decorated_name);
    return va ? va + loader_game()->delta : 0;
}

static RonResult api_hook_inline(RonPlugin *self, uintptr_t at, const uint8_t *sig, const uint8_t *mask,
                                 uint32_t sig_len, uint32_t stolen, void *detour, void **orig)
{
    if (owner_of(self) < 1)
        return RON_E_ARG;
    return hm_inline(owner_of(self), at, sig, mask, sig_len, stolen, detour, orig);
}

static RonResult api_hook_call(RonPlugin *self, uintptr_t call_site, void *detour, void **orig)
{
    if (owner_of(self) < 1)
        return RON_E_ARG;
    return hm_call(owner_of(self), call_site, detour, orig);
}

static RonResult api_hook_ptr(RonPlugin *self, uintptr_t slot, uintptr_t expected, void *detour, void **orig)
{
    if (owner_of(self) < 1)
        return RON_E_ARG;
    return hm_ptr(owner_of(self), slot, expected, detour, orig);
}

static RonResult api_patch_bytes(RonPlugin *self, uintptr_t at, const uint8_t *expect, const uint8_t *bytes,
                                 uint32_t len)
{
    if (owner_of(self) < 1)
        return RON_E_ARG;
    return hm_patch(owner_of(self), at, expect, bytes, len);
}

static RonResult api_on_event(RonPlugin *self, RonEvent ev, RonEventFn fn, void *user)
{
    if (owner_of(self) < 1)
        return RON_E_ARG;
    return ev_register(owner_of(self), ev, fn, user);
}

static const wchar_t *api_folder(RonPlugin *self) { return loader_owner_folder(owner_of(self)); }

static const RonApi k_api = {
    RON_API_MAJOR, RON_API_MINOR, api_log,        api_game,        api_va,       api_sym,
    api_hook_inline, api_hook_call, api_hook_ptr, api_patch_bytes, api_on_event, api_folder,
};

const RonApi *api_table(void) { return &k_api; }

/* "<PluginA> and <PluginB> both patch 0x<static address> (shared|conflict)" */
void api_notify(int owner, int other, uintptr_t at, int kind)
{
    log_line("loader", "%s and %s both patch 0x%06X (%s)", loader_owner_name(owner), loader_owner_name(other),
             (unsigned)(at - loader_game()->delta), kind == HM_SHARED ? "shared" : "conflict");
}
