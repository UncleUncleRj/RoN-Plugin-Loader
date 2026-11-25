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
} GameHook;

RonResult ev_register(int owner, RonEvent ev, RonEventFn fn, void *user);  /* RON_E_STATE after ev_lock */
void ev_fire(RonEvent ev, void *game_object);  /* owners ascending, then registration order */
void ev_drop_owner(int owner);
void ev_lock(void);
void ev_reset(void);

/* Installs the loader's own (owner 0) hooks on the six game functions; returns how many succeeded.
   A failed hook is logged and leaves its event silent. */
int ev_install_game_hooks(uintptr_t delta);
const GameHook *ev_game_hooks(int *count);

#endif
