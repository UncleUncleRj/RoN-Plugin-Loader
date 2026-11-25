/* Test plugin: hooks tests.exe's hk_add1, then returns 0, so the loader must roll the hook back. */
#include <windows.h>
#include "ron_plugin.h"

typedef int (*AddFn)(int);
static AddFn g_orig;
static int detour(int x) { return g_orig(x) * 10; }

RON_PLUGIN_EXPORT int __cdecl RonPluginMain(const RonApi *api, RonPlugin *self, RonPluginInfo *out)
{
    static const uint8_t sig[11] = {0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x83, 0xC0, 0x01, 0x5D, 0xC3};
    out->api_major = RON_API_MAJOR;
    out->affects_sim = 1;
    uintptr_t at = (uintptr_t)GetProcAddress(GetModuleHandleW(NULL), "hk_add1");
    if (at)
        api->hook_inline(self, at, sig, NULL, sizeof sig, 6, (void *)detour, (void **)&g_orig);
    return 0;
}
