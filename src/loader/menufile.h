/*
 * The Plugins button and panel in topmenu.xml (design section 8). The loader serves the game a copy
 * of its menu file with two additions: a "plugins" BUTTON in the full game's Tools & Extras panel
 * (TOPMENU/OPTIONS, after the "workshop" button, else above "back") and the PLUGINS panel element
 * before </TOPMENU>. TOPMENU_TRIAL is left alone; comments are skipped; tag and attribute names
 * match case-insensitively; the file's line ending and the indentation of the neighbouring line
 * are kept.
 */
#ifndef RON_MENUFILE_H
#define RON_MENUFILE_H

#include <stddef.h>

typedef enum { MF_FAILED = 0, MF_AFTER_WORKSHOP, MF_ABOVE_BACK, MF_ALREADY } MfWhere;

/* Writes the new text, NUL-terminated, to out and returns its length. *where tells where the button
   went (MF_ALREADY: a "plugins" button was there already; the panel is still added if missing).
   0 and MF_FAILED when nothing could be added: no TOPMENU block, no OPTIONS panel, neither
   workshop nor back, or cap too small. */
size_t menufile_add_plugins(const char *xml, size_t len, char *out, size_t cap, MfWhere *where);

#endif
