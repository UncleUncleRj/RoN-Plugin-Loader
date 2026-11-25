// Test plugin written in C++: the header alone must give RonPluginMain its C name.
#include "ron_plugin.h"

RON_PLUGIN_EXPORT int __cdecl RonPluginMain(const RonApi *api, RonPlugin *self, RonPluginInfo *out)
{
    out->api_major = RON_API_MAJOR;
    out->affects_sim = 0;
    api->log(self, "cpp loaded");
    return 1;
}
