/* Test plugin: tries to hook during pre_init, which the loader must refuse (hooks only in RonPluginMain). */
#include <windows.h>
#include "ron_plugin.h"

typedef int (*AddFn)(int);
static const RonApi *g_api;
static RonPlugin *g_self;
static AddFn g_orig;
static int detour(int x) { return g_orig(x) * 10; }

static void on_pre_init(RonEvent ev, void *game_object, void *user)
{
    static const uint8_t sig[11] = {0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x83, 0xC0, 0x01, 0x5D, 0xC3};
    (void)ev;
    (void)game_object;
    (void)user;
    uintptr_t at = (uintptr_t)GetProcAddress(GetModuleHandleW(NULL), "hk_add1");
    RonResult r = g_api->hook_inline(g_self, at, sig, NULL, sizeof sig, 6, (void *)detour, (void **)&g_orig);
    g_api->log(g_self, "late hook %d", (int)r);
}

RON_PLUGIN_EXPORT int __cdecl RonPluginMain(const RonApi *api, RonPlugin *self, RonPluginInfo *out)
{
    g_api = api;
    g_self = self;
    out->api_major = RON_API_MAJOR;
    out->affects_sim = 0;
    return api->on_event(self, RON_EV_PRE_INIT, on_pre_init, NULL) == RON_OK;
}
