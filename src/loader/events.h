/* The seven loader events: who is called, in what order, and the loader's hooks on the game
 * functions that fire them. Callbacks run on the game's main thread, owners in load order. */
#ifndef RON_EVENTS_H
#define RON_EVENTS_H

#include <stdint.h>
#include "ron_plugin.h"

typedef struct GameHook {
    RonEvent ev;
    uint32_t va;          /* static address from rise_z.map */
    uint8_t sig[10], mask[10];
    uint32_t sig_len, stolen;
    int call_site;        /* 1: va is an E8 call to the function, hooked with hm_call (stolen 0) */
} GameHook;

RonResult ev_register(int owner, RonEvent ev, RonEventFn fn, void *user);  /* RON_E_STATE after ev_lock */
void ev_fire(RonEvent ev, void *game_object);  /* owners ascending, then registration order */
void ev_drop_owner(int owner);
void ev_lock(void);
void ev_reset(void);

/* Called by the game hooks: match_built always fires; match_end fires on the first Game::close after a
   match was built (the game also calls Game::close to clear before a match, and twice at its end). */
void ev_match_built(void *setup);
void ev_game_closed(void *game);
/* A library the game loaded (d3dgl.dll, dssl.dll): RON_EV_LIBRARY once per module, with the hook
   manager open for addresses inside it while the callbacks run. */
void ev_library_loaded(void *module);

/* Installs the loader's own (owner 0) hooks on the eight game functions and call sites; returns how many succeeded.
   A failed hook is logged and leaves its event silent. */
int ev_install_game_hooks(uintptr_t delta);
int ev_installed(RonEvent ev);  /* 1 when that event's game hook took */
const GameHook *ev_game_hooks(int *count);

#endif
