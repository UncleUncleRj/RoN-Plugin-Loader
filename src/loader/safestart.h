/*
 * Safe start (design section 8). A marker file, <plugins>\.start-marker, says how far the current
 * start got: "starting", "loading <id>" while each plugin loads, "loaded" after the last one. It is
 * deleted once the main menu is up. Found at the next start, it means that start never reached the
 * menu (a plugin crashed or hung the game), so this start loads no plugins.
 */
#ifndef RON_SAFESTART_H
#define RON_SAFESTART_H

#include <stddef.h>
#include <wchar.h>

/* 1 when the marker of an earlier start is still there; culprit gets the id it names ("loading
   <id>") or L"". Then writes a fresh "starting" marker. 0 when there was no marker, or the marker
   cannot be read or written (later calls then do nothing). */
int ss_begin(const wchar_t *plugins_dir, wchar_t *culprit, size_t cap);
void ss_loading(const wchar_t *id);  /* marker: "loading <id>" */
void ss_loaded(void);                /* marker: "loaded" */
void ss_menu_reached(void);          /* deletes the marker */

#endif
