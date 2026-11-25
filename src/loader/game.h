/*
 * The engine calls the Plugins panel needs, each with the convention verified in the RE ledgers
 * (decomp/ron-ee-6674863f/notes/2026-11-24-plugins-panel-menu.txt and -draw.txt, root repo), never
 * the one the decorated name suggests. Static addresses; game_init adds the load delta.
 */
#ifndef RON_GAME_H
#define RON_GAME_H

#include <stdint.h>
#include <wchar.h>

/* The game's String: 0x14 bytes, built only by its constructors, freed with gs_close. */
typedef struct {
    unsigned char b[0x14];
} __attribute__((aligned(4))) GString;

void game_init(uintptr_t delta);

void gs_from_ascii(GString *s, const char *a);        /* String::String(char const*) 0xA1D660, copies */
void gs_from_static(GString *s, const wchar_t *w);    /* String::String(wchar_t const*) 0xA1EDD0: keeps w, which
                                                         must live for the whole process */
void gs_close(GString *s);                            /* String::close 0xA1CF40 */
int gs_is(const void *s, const char *ascii);          /* the String's text equals ascii, case-insensitively */
int gs_text(const void *s, wchar_t *out, int cap);    /* its text; 0 when empty or too long */

int game_sub_menu(void *topmenu, const char *panel);  /* TopMenu::sub_menu 0x59C030: runs a topmenu.xml panel */
void game_restart(void);                              /* WorkshopWin::reloadAssets 0x6AC3E0: the game's own restart */
int game_data_path(const wchar_t *name, wchar_t *out, int cap);  /* "data\<name>" after mods (prepend_content_dir) */
void game_click(void);                                /* the menu buttons' click sound */

#endif
