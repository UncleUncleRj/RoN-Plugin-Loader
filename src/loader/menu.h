/* The Plugins button and panel in the main menu (design section 8): the loader's own hooks on the
   game's menu code. */
#ifndef RON_MENU_H
#define RON_MENU_H

#include <stdint.h>

/* Installs the six owner-0 menu hooks, after every plugin has loaded; returns how many succeeded. */
int menu_install(uintptr_t delta, int *frame_hook);  /* *frame_hook: the panels' redraw hook took */

#endif
