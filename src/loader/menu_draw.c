/*
 * The Plugins panel's list (menu_draw.h; plan task 6): drawn after the game's own drawing of the
 * panel, in the IFACERECTs the generated panel declares, in the Steam Workshop window's style
 * (ledger draw F8, F9, F13): a dark pane (the Workshop's 0xD2000000), section titles in its title
 * font, a Load Order list (WHITE rows when on, GREY when off, the selected row filled with its colour
 * and written in BLACK, the Workshop check boxes) and a Description box for the selected row. Clicks
 * and the wheel act on the same layout (panel_layout / panel_hit). The modal loop redraws every
 * frame, so no invalidation is needed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "loader/menu_draw.h"
#include "loader/game.h"
#include "loader/hooks.h"
#include "loader/loader.h"

#define THISCALL __attribute__((thiscall))
#define TEXT_SIZE 12
#define TEXT_DY 4      /* text top inside a 24-px row */
#define NOTICE_LINE 13
#define PAD       8            /* text inset inside a section */

typedef void(THISCALL *RedrawFn)(void *sel);
typedef void(THISCALL *ClickFn)(void *sel, int x, int y);
typedef void(THISCALL *WheelFn)(void *sel, int delta, int x, int y);
static RedrawFn o_redraw;
static ClickFn o_click;
static WheelFn o_wheel;

static void rect(short r[4], int l, int rt, int t, int b)
{
    r[0] = (short)l;
    r[1] = (short)rt;
    r[2] = (short)t;
    r[3] = (short)b;
}

/* Characters that fit across an area px wide in the panel's size-12 font, about 6.9 px each: 60 on
   the stock 434-px list, fewer in a mod's narrower menu (the generated panel copies its width). */
static int chars_across(int px)
{
    int c = (px - 2 * PAD) * 10 / 69;
    return c < 12 ? 12 : c > 120 ? 120 : c;
}

/* Word wrap of an ASCII text into up to max lines of at most width characters. */
static int wrap_words(const char *s, char lines[][128], int max, int width)
{
    int n = 0;
    while (*s && n < max) {
        while (*s == ' ')
            s++;
        size_t len = strlen(s);
        size_t cut = len <= (size_t)width ? len : (size_t)width;
        if (cut < len) {
            size_t sp = cut;
            while (sp > 0 && s[sp] != ' ')
                sp--;
            if (sp > 0)
                cut = sp;
        }
        if (cut > 127)
            cut = 127;
        memcpy(lines[n], s, cut);
        lines[n][cut] = 0;
        n++;
        s += cut;
    }
    return n;
}

#define PANE_RGBA 0xD2000000u  /* black, alpha 0xD2: WorkshopWin::on_redraw fills its pane so (0x6AE42F) */
#define EDGE_RGBA 0xFF646464u  /* the sections' 1-px outline */

/* A 1-px outline just inside r, drawn with the fill (same batch, so in add order). */
static void outline(const short *R)
{
    short e[4];
    rect(e, R[0], R[1], R[2], R[2] + 1);
    game_fill_rgba(e, EDGE_RGBA);
    rect(e, R[0], R[1], R[3] - 1, R[3]);
    game_fill_rgba(e, EDGE_RGBA);
    rect(e, R[0], R[0] + 1, R[2], R[3]);
    game_fill_rgba(e, EDGE_RGBA);
    rect(e, R[1] - 1, R[1], R[2], R[3]);
    game_fill_rgba(e, EDGE_RGBA);
}

static void title(void *sel, const char *rect_name, const char *text)
{
    const short *T = game_find_rect(sel, rect_name);
    if (!T)
        return;
    game_text_begin();
    game_title(sel, text, T);
    game_text_end();
}

static void draw_row(void *sel, const short *L, const PanelSlot *slot, void *box_off, void *box_on)
{
    const PanelRow *r = &g_panel.rows[slot->row];
    int top = L[2] + slot->y, left = L[0], right = L[1];
    int selected = slot->row == g_panel.selected;
    int color = r->enabled ? GC_WHITE : GC_GREY;
    int ink = selected ? GC_BLACK : color;
    short rr[4];
    char num[16];

    if (selected) {
        rect(rr, left + 1, right - 1, top, top + PANEL_ROW_H);
        game_fill(rr, color);
    }

    /* Everything on the row goes one layer in front of the fill: quads are drawn grouped by
       texture, not in add order, so a check box at the fill's depth can land under it (ledger
       draw F7; seen in the game: the selected row had no box). */
    game_text_begin();
    game_tex(box_off, left + PANEL_BOX_X, top + 1, GC_WHITE);
    if (r->enabled)
        game_tex(box_on, left + PANEL_BOX_X, top + 1, GC_WHITE);
    snprintf(num, sizeof num, "%d", slot->row + 1);
    rect(rr, left, left + PANEL_BOX_X - 4, top + TEXT_DY, top + PANEL_ROW_H);
    game_text(sel, num, rr, GT_RIGHT, ink, TEXT_SIZE);
    rect(rr, left + PANEL_NAME_X, right - PAD, top + TEXT_DY, top + PANEL_ROW_H);
    int edge = game_text(sel, r->version, rr, GT_RIGHT, ink, TEXT_SIZE);
    const char *tag = r->failed ? "failed" : r->is_new ? "new" : NULL;
    if (tag) {
        rect(rr, left + PANEL_NAME_X, edge - 10, top + TEXT_DY, top + PANEL_ROW_H);
        edge = game_text(sel, tag, rr, GT_RIGHT, selected ? GC_BLACK : r->failed ? GC_RED : GC_YELLOW, TEXT_SIZE);
    }
    rect(rr, left + PANEL_NAME_X, edge - 10, top + TEXT_DY, top + PANEL_ROW_H);
    game_text(sel, r->name, rr, GT_LEFT, ink, TEXT_SIZE);
    game_text_end();
}

static void draw_description(void *sel)
{
    const short *D = game_find_rect(sel, "plugin_description");
    if (!D)
        return;
    outline(D);
    PanelLine lines[PANEL_DESC_LINES];
    int n = panel_description(&g_panel, lines, chars_across(D[1] - D[0]));
    short rr[4];
    game_text_begin();
    for (int i = 0; i < n; i++) {
        int y = D[2] + 6 + i * PANEL_LINE_H;
        int tone = lines[i].tone;
        rect(rr, D[0] + PAD, D[1] - PAD, y, y + PANEL_LINE_H);
        game_text(sel, lines[i].text, rr, GT_LEFT,
                  tone == PANEL_TONE_BAD ? GC_RED : tone == PANEL_TONE_DIM ? GC_GREY : GC_WHITE, TEXT_SIZE);
    }
    game_text_end();
}

static void draw_list(void *sel)
{
    const short *P = game_find_rect(sel, "plugin_pane");
    if (P)
        game_fill_rgba(P, PANE_RGBA);
    title(sel, "plugin_list_title", "Load Order");
    title(sel, "plugin_description_title", "Description");

    const short *L = game_find_rect(sel, "plugin_list");
    if (!L)
        return;
    outline(L);
    int list_h = L[3] - L[2];
    short rr[4];

    game_text_begin();
    rect(rr, L[0] + PAD, L[1] - PAD, L[2] + 3, L[2] + 3 + PANEL_LINE_H);
    game_text(sel, "Top loads first.", rr, GT_LEFT, GC_GREY, TEXT_SIZE);
    rect(rr, L[0] + PAD, L[1] - PAD, L[2] + 3 + PANEL_LINE_H, L[2] + 3 + 2 * PANEL_LINE_H);
    /* the long second line where it fits (the stock width), else a short one */
    static const char k_intro2[] = "Lower plugins win when two change the same thing.";
    int across = chars_across(L[1] - L[0]);
    game_text(sel, (int)sizeof k_intro2 - 1 <= across ? k_intro2 : "The lowest plugin wins.", rr, GT_LEFT, GC_GREY,
              TEXT_SIZE);
    /* More plugins than rows fit: say so beside the intro (the wheel scrolls the list). */
    int above, below;
    panel_more(&g_panel, list_h, &above, &below);
    char more[32];
    if (above) {
        snprintf(more, sizeof more, "%d more above", above);
        rect(rr, L[0] + PAD, L[1] - PAD, L[2] + 3, L[2] + 3 + PANEL_LINE_H);
        game_text(sel, more, rr, GT_RIGHT, GC_WHITE, TEXT_SIZE);
    }
    if (below) {
        snprintf(more, sizeof more, "%d more below", below);
        rect(rr, L[0] + PAD, L[1] - PAD, L[2] + 3 + PANEL_LINE_H, L[2] + 3 + 2 * PANEL_LINE_H);
        game_text(sel, more, rr, GT_RIGHT, GC_WHITE, TEXT_SIZE);
    }
    if (g_panel.n == 0) {
        char lines[3][128];
        int n = wrap_words("No plugins installed. Put a plugin's .dll and .ini in the plugins folder.", lines, 3, across);
        for (int i = 0; i < n; i++) {
            int y = L[2] + PANEL_INTRO_H + i * PANEL_LINE_H;
            rect(rr, L[0] + PAD, L[1] - PAD, y, y + PANEL_LINE_H);
            game_text(sel, lines[i], rr, GT_LEFT, GC_GREY, TEXT_SIZE);
        }
    }
    game_text_end();

    void *box_off = game_find_tex(sel, "check_off"), *box_on = game_find_tex(sel, "check_on");
    PanelSlot slots[ORDER_MAX];
    int n = panel_layout(&g_panel, list_h, slots, ORDER_MAX);
    for (int i = 0; i < n; i++)
        draw_row(sel, L, &slots[i], box_off, box_on);

    draw_description(sel);

    const char *notice = panel_notice(&g_panel);
    const short *N = game_find_rect(sel, "plugin_notice");
    if (notice && N) {
        char lines[6][128];
        int rows = (N[3] - N[2]) / NOTICE_LINE;
        int k = wrap_words(notice, lines, rows < 1 ? 1 : rows > 6 ? 6 : rows, chars_across(N[1] - N[0]));
        game_text_begin();
        for (int i = 0; i < k; i++) {
            rect(rr, N[0] + PAD, N[1] - PAD, N[2] + i * NOTICE_LINE, N[2] + (i + 1) * NOTICE_LINE);
            game_text(sel, lines[i], rr, GT_LEFT, GC_YELLOW, TEXT_SIZE);
        }
        game_text_end();
    }
}

/* TopMenuSelection::on_redraw: virtual thiscall, no arguments (ledger draw F1). */
static void THISCALL d_redraw(void *sel)
{
    o_redraw(sel);
    loader_menu_frame();  /* a menu frame got drawn, plugin hooks included: this start is good */
    if (menu_is_plugins_panel(sel))
        draw_list(sel);
}

/* Window::on_left_click(x, y) on release, window coordinates, ret 8 (ledger draw F10). */
static void THISCALL d_click(void *sel, int x, int y)
{
    o_click(sel, x, y);
    if (!menu_is_plugins_panel(sel))
        return;
    const short *L = game_find_rect(sel, "plugin_list");
    if (!L || x < L[0] || x >= L[1] || y < L[2] || y >= L[3])
        return;
    int on_box = 0;
    int row = panel_hit(&g_panel, L[3] - L[2], x - L[0], y - L[2], &on_box);
    if (row < 0)
        return;
    if (on_box)
        panel_toggle(&g_panel, row);
    else
        panel_select(&g_panel, row);
    game_click();
}

/* Window::on_mouse_wheel(delta, x, y), ret 0xC; one unit or 120 per notch, forward = up (ledger draw F10). */
static void THISCALL d_wheel(void *sel, int delta, int x, int y)
{
    if (menu_is_plugins_panel(sel)) {
        const short *L = game_find_rect(sel, "plugin_list");
        if (L && x >= L[0] && x < L[1] && y >= L[2] && y < L[3]) {
            int notches = abs(delta) >= 120 ? delta / 120 : delta;
            panel_scroll(&g_panel, L[3] - L[2], notches);
            return;
        }
    }
    o_wheel(sel, delta, x, y);
}

int menu_draw_install(uintptr_t vt, uintptr_t delta, int *redraw_ok)
{
    int ok = 0;
    *redraw_ok = hm_ptr(0, vt + 0xB8, 0x59EE80 + delta, (void *)d_redraw, (void **)&o_redraw) == RON_OK;
    ok += *redraw_ok;
    ok += hm_ptr(0, vt + 0xD8, 0x41C180 + delta, (void *)d_click, (void **)&o_click) == RON_OK;
    ok += hm_ptr(0, vt + 0x114, 0x41C360 + delta, (void *)d_wheel, (void **)&o_wheel) == RON_OK;
    return ok;
}
