/* The hook manager: every code or data patch made by the loader or a plugin goes through here.
 * Owner 0 is the loader, owners 1..n are plugins in load order. Hooks on one address chain:
 * the newest (lowest) hook runs first, its *orig is the hook before it, ending at the game's code. */
#ifndef RON_HOOKS_H
#define RON_HOOKS_H

#include <stdint.h>
#include "ron_plugin.h"

#define HM_SHARED 1
#define HM_CONFLICT 2

typedef void (*HmNotify)(int owner, int other, uintptr_t at, int kind);

RonResult hm_inline(int owner, uintptr_t at, const uint8_t *sig, const uint8_t *mask, uint32_t sig_len,
                    uint32_t stolen, void *detour, void **orig);
RonResult hm_call(int owner, uintptr_t site, void *detour, void **orig);
RonResult hm_ptr(int owner, uintptr_t slot, uintptr_t expected, void *detour, void **orig);
RonResult hm_patch(int owner, uintptr_t at, const uint8_t *expect, const uint8_t *bytes, uint32_t len);

/* How many distinct addresses the owner has a link or a byte patch at (the panel's "Hooks n game
   functions"). */
int hm_owner_count(int owner);

/* Undoes the owner's links and patches (the owner is the newest link in every chain it touched). */
void hm_rollback_owner(int owner);
void hm_lock(void);    /* afterwards every request returns RON_E_STATE, except inside an open window */
/* While locked, accept requests that lie wholly inside [lo, hi) (a library being announced by
   RON_EV_LIBRARY); hm_window(0, 0) closes it. */
void hm_window(uintptr_t lo, uintptr_t hi);
void hm_reset(void);   /* roll back every owner, newest first, and unlock (tests) */
void hm_set_notify(HmNotify fn);

#endif
