/* Test plugin: registers pre_init, checks its folder (plugins\GoodFiles), stays loaded. */
#include <wchar.h>
#include "ron_plugin.h"

static const RonApi *g_api;
static RonPlugin *g_self;

static void on_pre_init(RonEvent ev, void *game_object, void *user)
{
    (void)ev;
    (void)game_object;
    (void)user;
    g_api->log(g_self, "pre_init");
}

RON_PLUGIN_EXPORT int __cdecl RonPluginMain(const RonApi *api, RonPlugin *self, RonPluginInfo *out)
{
    g_api = api;
    g_self = self;
    out->api_major = RON_API_MAJOR;
    out->affects_sim = 0;
    const wchar_t *folder = api->folder(self);
    const wchar_t *want = L"\\plugins\\GoodFiles";
    size_t n = folder ? wcslen(folder) : 0, w = wcslen(want);
    if (n < w || _wcsicmp(folder + n - w, want) != 0)
        return 0;
    return api->on_event(self, RON_EV_PRE_INIT, on_pre_init, NULL) == RON_OK;
}
