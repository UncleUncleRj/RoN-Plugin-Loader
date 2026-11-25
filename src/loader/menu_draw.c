/*
 * The Plugins panel's list (menu_draw.h; plan task 6): drawn after the game's own drawing of the
 * panel, in the IFACERECT "plugin_list" the generated panel declares, in the Steam Workshop window's
 * style (ledger draw F8, F9): WHITE rows when on, GREY when off, the selected row filled with its
 * colour and written in BLACK, the Workshop check boxes. Clicks and the wheel act on the same layout
 * (panel_layout / panel_hit). The modal loop redraws every frame, so no invalidation is needed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "loader/menu_draw.h"
#include "loader/game.h"
#include "loader/hooks.h"

#define THISCALL __attribute__((thiscall))
#define TEXT_SIZE 12
#define TEXT_DY 4      /* text top inside a 24-px row */
#define NOTICE_LINE 13

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
        rect(rr, left, right, top, top + PANEL_ROW_H);
        game_fill(rr, color);
    }
    game_tex(box_off, left + PANEL_BOX_X, top + 1, GC_WHITE);
    if (r->enabled)
        game_tex(box_on, left + PANEL_BOX_X, top + 1, GC_WHITE);

    game_text_begin();
    snprintf(num, sizeof num, "%d", slot->row + 1);
    rect(rr, left, left + PANEL_BOX_X - 4, top + TEXT_DY, top + PANEL_ROW_H);
    game_text(sel, num, rr, 1, ink, TEXT_SIZE);
    rect(rr, left + PANEL_NAME_X, right - 4, top + TEXT_DY, top + PANEL_ROW_H);
    int edge = game_text(sel, r->version, rr, 1, ink, TEXT_SIZE);
    const char *tag = r->failed ? "failed" : r->is_new ? "new" : NULL;
    if (tag) {
        rect(rr, left + PANEL_NAME_X, edge - 10, top + TEXT_DY, top + PANEL_ROW_H);
        edge = game_text(sel, tag, rr, 1, selected ? GC_BLACK : r->failed ? GC_RED : GC_YELLOW, TEXT_SIZE);
    }
    rect(rr, left + PANEL_NAME_X, edge - 10, top + TEXT_DY, top + PANEL_ROW_H);
    game_text(sel, r->name, rr, 0, ink, TEXT_SIZE);
    if (selected) {
        char lines[4][128];
        int n = panel_details(&g_panel, slot->row, lines);
        for (int i = 0; i < n; i++) {
            int y = top + PANEL_ROW_H + 2 + i * PANEL_LINE_H;
            rect(rr, left + PANEL_NAME_X, right - 4, y, y + PANEL_LINE_H);
            game_text(sel, lines[i], rr, 0, GC_GREY, TEXT_SIZE);
        }
    }
    game_text_end();
}

static void draw_list(void *sel)
{
    const short *L = game_find_rect(sel, "plugin_list");
    if (!L)
        return;
    int list_h = L[3] - L[2];
    short rr[4];

    game_text_begin();
    rect(rr, L[0], L[1], L[2], L[2] + PANEL_LINE_H);
    game_text(sel, "Top loads first.", rr, 0, GC_GREY, TEXT_SIZE);
    rect(rr, L[0], L[1], L[2] + PANEL_LINE_H, L[2] + 2 * PANEL_LINE_H);
    game_text(sel, "Lower plugins win when two change the same thing.", rr, 0, GC_GREY, TEXT_SIZE);
    if (g_panel.n == 0) {
        char lines[3][128];
        int n = wrap_words("No plugins installed. Put a plugin's .dll and .ini in the plugins folder.", lines, 3, 60);
        for (int i = 0; i < n; i++) {
            int y = L[2] + PANEL_INTRO_H + i * PANEL_LINE_H;
            rect(rr, L[0], L[1], y, y + PANEL_LINE_H);
            game_text(sel, lines[i], rr, 0, GC_GREY, TEXT_SIZE);
        }
    }
    game_text_end();

    void *box_off = game_find_tex(sel, "check_off"), *box_on = game_find_tex(sel, "check_on");
    PanelSlot slots[ORDER_MAX];
    int n = panel_layout(&g_panel, list_h, slots, ORDER_MAX);
    for (int i = 0; i < n; i++)
        draw_row(sel, L, &slots[i], box_off, box_on);

    const char *notice = panel_notice(&g_panel);
    const short *N = game_find_rect(sel, "plugin_notice");
    if (notice && N) {
        char lines[3][128];
        int k = wrap_words(notice, lines, 3, 60);
        game_text_begin();
        for (int i = 0; i < k; i++) {
            rect(rr, N[0], N[1], N[2] + i * NOTICE_LINE, N[2] + (i + 1) * NOTICE_LINE);
            game_text(sel, lines[i], rr, 0, GC_YELLOW, TEXT_SIZE);
        }
        game_text_end();
    }
}

/* TopMenuSelection::on_redraw: virtual thiscall, no arguments (ledger draw F1). */
static void THISCALL d_redraw(void *sel)
{
    o_redraw(sel);
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

int menu_draw_install(uintptr_t vt, uintptr_t delta)
{
    int ok = 0;
    ok += hm_ptr(0, vt + 0xB8, 0x59EE80 + delta, (void *)d_redraw, (void **)&o_redraw) == RON_OK;
    ok += hm_ptr(0, vt + 0xD8, 0x41C180 + delta, (void *)d_click, (void **)&o_click) == RON_OK;
    ok += hm_ptr(0, vt + 0x114, 0x41C360 + delta, (void *)d_wheel, (void **)&o_wheel) == RON_OK;
    return ok;
}
