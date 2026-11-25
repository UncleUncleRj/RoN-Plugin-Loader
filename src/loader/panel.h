/*
 * The Plugins panel's state (design section 8): the rows in their edited order and on/off state,
 * the selection and the scroll position, against the order and state read at this start. Also its
 * texts, its layout inside the list area and hit testing, and saving load-order.txt. No game code:
 * menu.c and menu_draw.c show it.
 */
#ifndef RON_PANEL_H
#define RON_PANEL_H

#include <stddef.h>
#include <wchar.h>
#include "loader/loader.h"

#define PANEL_INTRO_H 36   /* intro: two 16-px lines and a 4-px gap above the first row */
#define PANEL_ROW_H   24
#define PANEL_LINE_H  16   /* one line of the description box */
#define PANEL_BOX_X   28   /* check box, from the list's left edge; 25 x 22, vertically centred in the row */
#define PANEL_BOX_W   25
#define PANEL_NAME_X  60
#define PANEL_WRAP    60   /* characters per description line, at most 2 lines, "..." when cut */
#define PANEL_DESC_LINES 6 /* name; version and author; description (2); file; status */

typedef struct PanelRow {
    wchar_t id[64];
    char name[64], version[32], author[64], description[256], reason[128];
    int enabled, is_new, failed, loaded, hooks, files;
} PanelRow;

typedef struct Panel {
    PanelRow rows[ORDER_MAX];
    int n, selected, scroll, safe_start, save_failed;
    wchar_t start_ids[ORDER_MAX][64];  /* the order and on/off state read at this start */
    int start_enabled[ORDER_MAX];
} Panel;

enum { PANEL_TONE_TEXT, PANEL_TONE_DIM, PANEL_TONE_BAD };

typedef struct PanelLine {
    char text[128];  /* UTF-8 */
    int tone;        /* PANEL_TONE_* */
} PanelLine;

typedef struct PanelSlot {
    int row, y, h;  /* y from the list's top */
} PanelSlot;

void panel_init(Panel *p, const LoaderPlugin *plugins, int n, const int *hooks, int safe_start);
void panel_set_files(Panel *p, const int *files);  /* how many files each row's plugin serves */
void panel_select(Panel *p, int row);           /* -1 clears */
void panel_toggle(Panel *p, int row);
int panel_move(Panel *p, int delta);            /* the selected row, -1 up / +1 down; 1 if it moved */
int panel_changed(const Panel *p);
/* The selected row's lines, or a hint; the line count. wrap: characters per description line
   (PANEL_WRAP at most; the box's width decides). */
int panel_description(const Panel *p, PanelLine out[PANEL_DESC_LINES], int wrap);
int panel_layout(const Panel *p, int list_h, PanelSlot *out, int max);  /* visible rows only */
int panel_hit(const Panel *p, int list_h, int x, int y, int *on_box);   /* row or -1 */
void panel_scroll(Panel *p, int list_h, int notches);                   /* positive = toward row 0 */
void panel_keep_visible(Panel *p, int list_h);                          /* scrolls the selected row into view */
void panel_more(const Panel *p, int list_h, int *above, int *below);    /* rows hidden above and below */
const char *panel_notice(const Panel *p);       /* NULL or a notice */
int panel_save(Panel *p, const wchar_t *path);  /* 0 and save_failed = 1 on failure */

#endif
