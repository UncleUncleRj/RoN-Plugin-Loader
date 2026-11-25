/*
 * HelloPlugin: the smallest useful plugin. It logs every loader event once, counts simulation
 * frames two ways (the sim_frame event, and its own hook on Game::do_frame, which chains with the
 * loader's hook on the same function), and reports both counts when the match ends.
 */
#include "ron_plugin.h"

#define THISCALL __attribute__((thiscall))

static const RonApi *g_api;
static RonPlugin *g_self;
static unsigned g_sim_frames, g_hook_calls, g_render_frames;

typedef void(THISCALL *DoFrameFn)(void *game);
static DoFrameFn g_do_frame;

static void THISCALL do_frame_hook(void *game)
{
    g_hook_calls++;
    g_do_frame(game);
}

static void on_event(RonEvent ev, void *game_object, void *user)
{
    (void)game_object;
    (void)user;
    switch (ev) {
    case RON_EV_PRE_INIT:
        g_api->log(g_self, "pre_init");
        break;
    case RON_EV_POST_INIT:
        g_api->log(g_self, "post_init");
        break;
    case RON_EV_MATCH_BUILT:
        g_api->log(g_self, "match_built");
        break;
    case RON_EV_SIM_FRAME:
        if (g_sim_frames++ == 0)
            g_api->log(g_self, "sim_frame first");
        break;
    case RON_EV_RENDER_FRAME:
        if (g_render_frames++ == 0)
            g_api->log(g_self, "render_frame first");
        break;
    case RON_EV_MATCH_END:
        g_api->log(g_self, "match_end sim_frames=%u hook_calls=%u", g_sim_frames, g_hook_calls);
        break;
    case RON_EV_SHUTDOWN:
        g_api->log(g_self, "shutdown");
        break;
    default:
        break;
    }
}

RON_PLUGIN_EXPORT int __cdecl RonPluginMain(const RonApi *api, RonPlugin *self, RonPluginInfo *out)
{
    static const uint8_t sig[5] = {0x55, 0x8B, 0xEC, 0x6A, 0xFF};
    g_api = api;
    g_self = self;
    out->api_major = RON_API_MAJOR;
    out->affects_sim = 0;
    for (int ev = 0; ev < RON_EV_COUNT; ev++)
        api->on_event(self, (RonEvent)ev, on_event, NULL);
    uintptr_t do_frame = api->sym("?do_frame@Game@@QAEXXZ");
    if (!do_frame) {
        api->log(self, "Game::do_frame not found in the symbol map");
        return 1;
    }
    RonResult r = api->hook_inline(self, do_frame, sig, NULL, sizeof sig, 5, (void *)do_frame_hook,
                                   (void **)&g_do_frame);
    if (r != RON_OK)
        api->log(self, "hook on Game::do_frame failed (%d)", (int)r);
    return 1;
}
