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

/* Drawing, in a window's own coordinates (ledger draw F1-F9). Rects are short left, right, top, bottom. */
enum { GC_WHITE, GC_GREY, GC_RED, GC_YELLOW, GC_BLACK };
const short *game_find_rect(void *win, const char *name);  /* an IFACERECT of the window's XML, or NULL */
void *game_find_tex(void *win, const char *name);          /* an IFACETEX of the window's XML, or NULL */
void game_tex(void *tex, int x, int y, int color);         /* IFaceRenderManager::add, at its own size */
void game_fill(const short r[4], int color);               /* IFaceRenderManager::add, solid */
void game_text_begin(void);                                /* what follows draws in front of the textures and
                                                              fills added before: depth + 1 */
void game_text_end(void);
/* utf8 in rect r (left or right aligned), size 12-ish, colour GC_*; returns the text's left x when
   right aligned, else r[0]. */
int game_text(void *win, const char *utf8, const short r[4], int right, int color, int size);

#endif
