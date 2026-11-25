/* Test plugin: claims an API version the loader doesn't have. */
#include "ron_plugin.h"

RON_PLUGIN_EXPORT int __cdecl RonPluginMain(const RonApi *api, RonPlugin *self, RonPluginInfo *out)
{
    (void)api;
    (void)self;
    out->api_major = 99;
    out->affects_sim = 0;
    return 1;
}
