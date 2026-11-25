/* The Plugins panel's list (menu_draw.h). Pass-through until task 6 of the plan. */
#include "loader/menu_draw.h"
#include "loader/hooks.h"

#define THISCALL __attribute__((thiscall))

typedef void(THISCALL *RedrawFn)(void *sel);
typedef void(THISCALL *ClickFn)(void *sel, int x, int y);
typedef void(THISCALL *WheelFn)(void *sel, int delta, int x, int y);
static RedrawFn o_redraw;
static ClickFn o_click;
static WheelFn o_wheel;

static void THISCALL d_redraw(void *sel) { o_redraw(sel); }
static void THISCALL d_click(void *sel, int x, int y) { o_click(sel, x, y); }
static void THISCALL d_wheel(void *sel, int delta, int x, int y) { o_wheel(sel, delta, x, y); }

int menu_draw_install(uintptr_t vt, uintptr_t delta)
{
    int ok = 0;
    ok += hm_ptr(0, vt + 0xB8, 0x59EE80 + delta, (void *)d_redraw, (void **)&o_redraw) == RON_OK;
    ok += hm_ptr(0, vt + 0xD8, 0x41C180 + delta, (void *)d_click, (void **)&o_click) == RON_OK;
    ok += hm_ptr(0, vt + 0x114, 0x41C360 + delta, (void *)d_wheel, (void **)&o_wheel) == RON_OK;
    return ok;
}
