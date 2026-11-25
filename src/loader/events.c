#include <string.h>
#include "loader/events.h"
#include "loader/hooks.h"
#include "loader/log.h"

#define MAX_CALLBACKS 512

typedef struct Callback {
    int owner;
    RonEvent ev;
    RonEventFn fn;
    void *user;
} Callback;

static Callback g_cb[MAX_CALLBACKS];  /* kept sorted by owner, then registration order */
static int g_ncb;
static int g_locked;

RonResult ev_register(int owner, RonEvent ev, RonEventFn fn, void *user)
{
    if (g_locked)
        return RON_E_STATE;
    if (!fn || ev < 0 || ev >= RON_EV_COUNT)
        return RON_E_ARG;
    if (g_ncb >= MAX_CALLBACKS)
        return RON_E_MEM;
    int at = g_ncb;
    while (at > 0 && g_cb[at - 1].owner > owner)
        at--;
    memmove(&g_cb[at + 1], &g_cb[at], (size_t)(g_ncb - at) * sizeof g_cb[0]);
    g_cb[at].owner = owner;
    g_cb[at].ev = ev;
    g_cb[at].fn = fn;
    g_cb[at].user = user;
    g_ncb++;
    return RON_OK;
}

void ev_fire(RonEvent ev, void *game_object)
{
    int n = g_ncb;
    for (int i = 0; i < n; i++) {
        if (g_cb[i].ev == ev)
            g_cb[i].fn(ev, game_object, g_cb[i].user);
    }
}

void ev_drop_owner(int owner)
{
    int w = 0;
    for (int i = 0; i < g_ncb; i++) {
        if (g_cb[i].owner != owner)
            g_cb[w++] = g_cb[i];
    }
    g_ncb = w;
}

void ev_lock(void) { g_locked = 1; }

void ev_reset(void)
{
    g_ncb = 0;
    g_locked = 0;
}

/* ---- the loader's hooks on game functions ----
 * Signatures are the stock bytes; mask 0x00 marks absolute addresses that ASLR relocates. */
static const GameHook k_hooks[6] = {
    {RON_EV_POST_INIT, 0x595BE0, {0x55, 0x8B, 0xEC, 0x6A, 0xFF}, {1, 1, 1, 1, 1}, 5, 5},
    {RON_EV_MATCH_BUILT, 0x5AC190, {0x53, 0x8B, 0xDC, 0x83, 0xEC, 0x08}, {1, 1, 1, 1, 1, 1}, 6, 6},
    {RON_EV_SIM_FRAME, 0x591EF0, {0x55, 0x8B, 0xEC, 0x6A, 0xFF}, {1, 1, 1, 1, 1}, 5, 5},
    {RON_EV_RENDER_FRAME, 0x5917E0, {0x55, 0x8B, 0xEC, 0x83, 0x3D, 0, 0, 0, 0, 0x00},
     {1, 1, 1, 1, 1, 0, 0, 0, 0, 1}, 10, 10},
    {RON_EV_MATCH_END, 0x594B80, {0x53, 0x8B, 0xDC, 0x83, 0xEC, 0x08}, {1, 1, 1, 1, 1, 1}, 6, 6},
    {RON_EV_SHUTDOWN, 0x5966A0, {0x57, 0x8B, 0xF9, 0xB9, 0, 0, 0, 0}, {1, 1, 1, 1, 0, 0, 0, 0}, 8, 8},
};
static const char *const k_names[6] = {"Main::init_menus",  "Setup::build_game", "Game::do_frame",
                                       "Game::loop_render", "Game::close",       "Main::shutdown"};

#define THISCALL __attribute__((thiscall))

typedef void(__cdecl *InitMenusFn)(unsigned int *);
typedef int(THISCALL *BuildGameFn)(void *self, int a, int b, int c);
typedef void(THISCALL *DoFrameFn)(void *self);
typedef int(THISCALL *LoopRenderFn)(void *self, int a, int b);
typedef void(THISCALL *CloseFn)(void *self, int a);
typedef void(THISCALL *ShutdownFn)(void *ecx, int a);  /* static, but its code reads ecx on entry */

static InitMenusFn o_init_menus;
static BuildGameFn o_build_game;
static DoFrameFn o_do_frame;
static LoopRenderFn o_loop_render;
static CloseFn o_close;
static ShutdownFn o_shutdown;

static void __cdecl d_init_menus(unsigned int *a)
{
    o_init_menus(a);
    ev_fire(RON_EV_POST_INIT, NULL);
}

static int THISCALL d_build_game(void *self, int a, int b, int c)
{
    int r = o_build_game(self, a, b, c);
    ev_fire(RON_EV_MATCH_BUILT, self);
    return r;
}

static void THISCALL d_do_frame(void *self)
{
    o_do_frame(self);
    ev_fire(RON_EV_SIM_FRAME, self);
}

static int THISCALL d_loop_render(void *self, int a, int b)
{
    int r = o_loop_render(self, a, b);
    ev_fire(RON_EV_RENDER_FRAME, self);
    return r;
}

static void THISCALL d_close(void *self, int a)
{
    ev_fire(RON_EV_MATCH_END, self);
    o_close(self, a);
}

static void THISCALL d_shutdown(void *ecx, int a)
{
    ev_fire(RON_EV_SHUTDOWN, NULL);
    o_shutdown(ecx, a);
}

const GameHook *ev_game_hooks(int *count)
{
    if (count)
        *count = 6;
    return k_hooks;
}

int ev_install_game_hooks(uintptr_t delta)
{
    void *const detours[6] = {(void *)d_init_menus, (void *)d_build_game, (void *)d_do_frame,
                              (void *)d_loop_render, (void *)d_close,     (void *)d_shutdown};
    void **const origs[6] = {(void **)&o_init_menus, (void **)&o_build_game, (void **)&o_do_frame,
                             (void **)&o_loop_render, (void **)&o_close,     (void **)&o_shutdown};
    int ok = 0;
    for (int i = 0; i < 6; i++) {
        const GameHook *h = &k_hooks[i];
        RonResult r = hm_inline(0, h->va + delta, h->sig, h->mask, h->sig_len, h->stolen, detours[i], origs[i]);
        if (r == RON_OK)
            ok++;
        else
            log_line("loader", "event hook on %s (0x%06X) failed (%d): that event stays silent", k_names[i],
                     (unsigned)h->va, (int)r);
    }
    return ok;
}
