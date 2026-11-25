/*
 * HelloPlugin: the smallest useful plugin. It logs every loader event once and counts frames two
 * ways: by event (sim_frame, render_frame) and by its own hooks on Game::do_frame and
 * Game::loop_render. The loop_render hook shares the function with the loader's own hook, so the
 * two counts match only when the chain works. It reports all four counts when a match ends.
 */
#include "ron_plugin.h"

#define THISCALL __attribute__((thiscall))

static const RonApi *g_api;
static RonPlugin *g_self;
static unsigned g_sim_frames, g_hook_calls, g_render_frames, g_render_hook_calls;

typedef void(THISCALL *DoFrameFn)(void *game);
static DoFrameFn g_do_frame;

static void THISCALL do_frame_hook(void *game)
{
    g_hook_calls++;
    g_do_frame(game);
}

typedef int(THISCALL *LoopRenderFn)(void *game, int a, int b);
static LoopRenderFn g_loop_render;

static int THISCALL loop_render_hook(void *game, int a, int b)
{
    g_render_hook_calls++;
    return g_loop_render(game, a, b);
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
        g_api->log(g_self, "match_end sim_frames=%u hook_calls=%u render_frames=%u render_hook_calls=%u",
                   g_sim_frames, g_hook_calls, g_render_frames, g_render_hook_calls);
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

    /* push ebp; mov ebp,esp; cmp dword ptr [abs32], 0 -- the absolute address moves with ASLR: masked */
    static const uint8_t rsig[10] = {0x55, 0x8B, 0xEC, 0x83, 0x3D, 0, 0, 0, 0, 0x00};
    static const uint8_t rmask[10] = {1, 1, 1, 1, 1, 0, 0, 0, 0, 1};
    uintptr_t loop_render = api->sym("?loop_render@Game@@QBEHHH@Z");
    r = loop_render ? api->hook_inline(self, loop_render, rsig, rmask, sizeof rsig, 10, (void *)loop_render_hook,
                                       (void **)&g_loop_render)
                    : RON_E_ARG;
    if (r != RON_OK)
        api->log(self, "hook on Game::loop_render failed (%d)", (int)r);
    return 1;
}
