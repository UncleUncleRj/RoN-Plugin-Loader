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
static int g_match_open;  /* a match was built and has not ended yet */

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
    if (g_locked) {  /* nothing can register any more: walk the list itself */
        for (int i = 0; i < g_ncb; i++) {
            if (g_cb[i].ev == ev)
                g_cb[i].fn(ev, game_object, g_cb[i].user);
        }
        return;
    }
    /* While plugins load (pre_init), a callback may register another one, which shifts the sorted
       list: walk a copy, so no callback is skipped or called twice. */
    Callback copy[MAX_CALLBACKS];
    int n = g_ncb;
    memcpy(copy, g_cb, (size_t)n * sizeof copy[0]);
    for (int i = 0; i < n; i++) {
        if (copy[i].ev == ev)
            copy[i].fn(ev, game_object, copy[i].user);
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
    g_match_open = 0;
}

/* ---- the loader's hooks on game functions ----
 * Signatures are the stock bytes; mask 0x00 marks absolute addresses that ASLR relocates. */
static const GameHook k_hooks[6] = {
    {RON_EV_POST_INIT, 0x595BE0, {0x55, 0x8B, 0xEC, 0x6A, 0xFF}, {1, 1, 1, 1, 1}, 5, 5},
    {RON_EV_MATCH_BUILT, 0x5AC190, {0x53, 0x8B, 0xDC, 0x83, 0xEC, 0x08}, {1, 1, 1, 1, 1, 1}, 6, 6},
    /* Game::do_frame (0x591EF0) has one caller, Game::loop+0x14E: hooking that call instead of the
       function's entry keeps sim_frame working when a tool has hooked the entry (rondbg's agent does) */
    {RON_EV_SIM_FRAME, 0x5916BE, {0xE8}, {1}, 1, 0, 1},
    {RON_EV_RENDER_FRAME, 0x5917E0, {0x55, 0x8B, 0xEC, 0x83, 0x3D, 0, 0, 0, 0, 0x00},
     {1, 1, 1, 1, 1, 0, 0, 0, 0, 1}, 10, 10},
    {RON_EV_MATCH_END, 0x594B80, {0x53, 0x8B, 0xDC, 0x83, 0xEC, 0x08}, {1, 1, 1, 1, 1, 1}, 6, 6},
    /* the call of Main::run_game_loop in Main::main (Main::main+0x913): it returns on quit. Main::shutdown
       itself is not reached on quit: the process ends in SteamAuthentication first (traced) */
    {RON_EV_SHUTDOWN, 0x561603, {0xE8}, {1}, 1, 0, 1},
};
static const char *const k_names[6] = {"Main::init_menus",  "Setup::build_game", "Game::loop -> Game::do_frame",
                                       "Game::loop_render", "Game::close",       "Main::main -> Main::run_game_loop"};

#define THISCALL __attribute__((thiscall))

typedef void(__cdecl *InitMenusFn)(unsigned int *);
typedef int(THISCALL *BuildGameFn)(void *self, int a, int b, int c);
typedef void(THISCALL *DoFrameFn)(void *self);
typedef int(THISCALL *LoopRenderFn)(void *self, int a, int b);
typedef void(THISCALL *CloseFn)(void *self, int a);
typedef int(__cdecl *RunGameLoopFn)(int);  /* static int Main::run_game_loop(int) */

static InitMenusFn o_init_menus;
static BuildGameFn o_build_game;
static DoFrameFn o_do_frame;
static LoopRenderFn o_loop_render;
static CloseFn o_close;
static RunGameLoopFn o_run_game_loop;

static void __cdecl d_init_menus(unsigned int *a)
{
    o_init_menus(a);
    ev_fire(RON_EV_POST_INIT, NULL);
}

static int THISCALL d_build_game(void *self, int a, int b, int c)
{
    int r = o_build_game(self, a, b, c);
    ev_match_built(self);
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
    ev_game_closed(self);
    o_close(self, a);
}

static int __cdecl d_run_game_loop(int a)
{
    int r = o_run_game_loop(a);
    ev_fire(RON_EV_SHUTDOWN, NULL);
    return r;
}

void ev_match_built(void *setup)
{
    g_match_open = 1;
    ev_fire(RON_EV_MATCH_BUILT, setup);
}

void ev_game_closed(void *game)
{
    if (!g_match_open)
        return;
    g_match_open = 0;
    ev_fire(RON_EV_MATCH_END, game);
}

const GameHook *ev_game_hooks(int *count)
{
    if (count)
        *count = 6;
    return k_hooks;
}

static int g_installed[RON_EV_COUNT];

int ev_installed(RonEvent ev) { return ev >= 0 && ev < RON_EV_COUNT && g_installed[ev]; }

int ev_install_game_hooks(uintptr_t delta)
{
    void *const detours[6] = {(void *)d_init_menus, (void *)d_build_game, (void *)d_do_frame,
                              (void *)d_loop_render, (void *)d_close,     (void *)d_run_game_loop};
    void **const origs[6] = {(void **)&o_init_menus, (void **)&o_build_game, (void **)&o_do_frame,
                             (void **)&o_loop_render, (void **)&o_close,     (void **)&o_run_game_loop};
    int ok = 0;
    for (int i = 0; i < 6; i++) {
        const GameHook *h = &k_hooks[i];
        RonResult r = h->call_site
                          ? hm_call(0, h->va + delta, detours[i], origs[i])
                          : hm_inline(0, h->va + delta, h->sig, h->mask, h->sig_len, h->stolen, detours[i], origs[i]);
        if (r == RON_OK) {
            ok++;
            g_installed[h->ev] = 1;
        } else
            log_line("loader", "event hook on %s (0x%06X) failed (%d): that event stays silent", k_names[i],
                     (unsigned)h->va, (int)r);
    }
    return ok;
}
