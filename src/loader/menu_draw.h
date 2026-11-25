/* The Plugins panel's list: drawn by the loader in the panel's free area, with its clicks and wheel. */
#ifndef RON_MENU_DRAW_H
#define RON_MENU_DRAW_H

#include <stdint.h>
#include "loader/panel.h"

extern Panel g_panel;                  /* menu.c: the panel being shown */
int menu_is_plugins_panel(void *sel);  /* menu.c: the TopMenuSelection shows the PLUGINS panel */

/* Hooks TopMenuSelection's on_redraw (+0xB8), on_left_click (+0xD8) and on_mouse_wheel (+0x114)
   in the virtual table at vt; returns how many of the 3 took. */
int menu_draw_install(uintptr_t vt, uintptr_t delta);

#endif
