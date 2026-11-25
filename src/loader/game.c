#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "loader/game.h"

#define THISCALL __attribute__((thiscall))

#define MAX_NAME_ASCII 240

static uintptr_t g_delta;
#define VA(x) ((uintptr_t)(x) + g_delta)

void game_init(uintptr_t delta) { g_delta = delta; }

/* Strings (RON Revived survival.c uses the same calls). */
typedef void(THISCALL *StrCtorA)(void *s, const char *a);
typedef void(THISCALL *StrCtorW)(void *s, const wchar_t *w);
typedef void(THISCALL *StrClose)(void *s);
typedef const wchar_t *(THISCALL *StrChars)(const void *s);

void gs_from_ascii(GString *s, const char *a) { ((StrCtorA)VA(0xA1D660))(s, a); }
void gs_from_static(GString *s, const wchar_t *w) { ((StrCtorW)VA(0xA1EDD0))(s, w); }
void gs_close(GString *s) { ((StrClose)VA(0xA1CF40))(s); }

static const wchar_t *chars(const void *s, int *len)
{
    *len = 0;
    if (!s || !*(void *const *)s)
        return NULL;
    const wchar_t *w = ((StrChars)VA(0xA1F0A0))(s);
    *len = *(const unsigned short *)((const char *)s + 8);
    return w;
}

int gs_is(const void *s, const char *ascii)
{
    int len;
    const wchar_t *w = chars(s, &len);
    if (!w || (size_t)len != strlen(ascii))
        return 0;
    for (int i = 0; i < len; i++) {
        wchar_t a = w[i], b = (unsigned char)ascii[i];
        if (a >= L'A' && a <= L'Z')
            a += 32;
        if (b >= L'A' && b <= L'Z')
            b += 32;
        if (a != b)
            return 0;
    }
    return 1;
}

int gs_text(const void *s, wchar_t *out, int cap)
{
    int len;
    const wchar_t *w = chars(s, &len);
    if (!w || len <= 0 || len >= cap) {
        if (cap)
            out[0] = 0;
        return 0;
    }
    memcpy(out, w, (size_t)len * sizeof(wchar_t));
    out[len] = 0;
    return 1;
}

/* TopMenu::sub_menu: thiscall(this, String const &), ret 4 (ledger menu F4). */
typedef int(THISCALL *SubMenu)(void *topmenu, void *name);

int game_sub_menu(void *topmenu, const char *panel)
{
    GString name;
    gs_from_ascii(&name, panel);
    int r = ((SubMenu)VA(0x59C030))(topmenu, &name);
    gs_close(&name);
    return r;
}

/* WorkshopWin::reloadAssets: no arguments, plain ret, ECX unused (ledger menu F4). */
void game_restart(void) { ((void (*)(void))VA(0x6AC3E0))(); }

/* String::prepend_content_dir: thiscall(src, hidden ret String*, int force, char *file, int line),
   ret 0x10 (ledger menu F6). */
typedef void *(THISCALL *Prepend)(void *src, void *ret, int force, const char *file, int line);

int game_data_path(const wchar_t *name, wchar_t *out, int cap)
{
    char a[MAX_NAME_ASCII];
    out[0] = 0;
    int n = 0;
    for (; name[n]; n++) {
        if (name[n] >= 128 || n + 6 >= (int)sizeof a)
            return 0;  /* only ASCII names are resolved (ledger ruling) */
        a[n] = (char)name[n];
    }
    a[n] = 0;
    char path[MAX_NAME_ASCII + 8];
    snprintf(path, sizeof path, "data\\%s", a);
    GString src, ret;
    gs_from_ascii(&src, path);
    ((Prepend)VA(0xA1D690))(&src, &ret, 0, "loader", 0);
    int ok = gs_text(&ret, out, cap);
    gs_close(&ret);
    gs_close(&src);
    return ok;
}

/* SoundGlobal::play(SoundGlobalCat): the category by value, ret 4, ECX ignored; 0xAF = the menu
   buttons' click (ledger draw F12). */
void game_click(void) { ((void(__stdcall *)(int))VA(0x97F770))(0xAF); }

/* ---- drawing (ledger draw; the same calls as RON Revived's assist_panel.c, tested in the game) ---- */

#define IFACE_RM     0xEBD1C0  /* IFaceRenderManager */
#define FONTS        0xED8160  /* FontFamily */
#define GRAPHSYS     0xE335A0  /* GraphSys *: +0x164 the depth of what is added */
#define GS_DEPTH     0x164
#define WIN_BUFFER   0x154     /* the window's Buffer */
#define BUF_FONT     0x7C      /* Font *, then two more words */
#define BUF_COLORS   0x90      /* what set_text_color changes */
#define BUF_COLORS_N 0x80
#define RENDER_SOLID 5

static const uintptr_t k_colors[] = {0xC8D26C, 0xC8D218, 0xC8D248, 0xC8D278, 0xC8D260};  /* WHITE GREY RED YELLOW BLACK */

static const void *color_of(int c) { return (const void *)VA(k_colors[c >= 0 && c <= GC_BLACK ? c : 0]); }
static unsigned rgba_of(int c) { return *(const unsigned *)color_of(c); }

typedef void *(THISCALL *FindFn)(void *win, const void *name, int id, int *out);  /* ret 0xC */

static void *find(void *win, const char *name, int slot)
{
    if (!win)
        return NULL;
    GString s;
    gs_from_ascii(&s, name);
    void *r = ((FindFn)(*(void ***)win)[slot / 4])(win, &s, -1, NULL);
    gs_close(&s);
    return r;
}

const short *game_find_rect(void *win, const char *name) { return find(win, name, 0x488); }
void *game_find_tex(void *win, const char *name) { return find(win, name, 0xA4); }

typedef void(THISCALL *AddXYFn)(void *rm, const void *tex, int x, int y, unsigned color, int type);       /* ret 0x14 */
typedef void(THISCALL *AddRectFn)(void *rm, const void *tex, const short *r, unsigned color, int type);   /* ret 0x10 */

void game_tex(void *tex, int x, int y, int color)
{
    if (tex)
        ((AddXYFn)VA(0x51F440))((void *)VA(IFACE_RM), tex, x, y, rgba_of(color), 1);
}

void game_fill_rgba(const short r[4], unsigned rgba)
{
    ((AddRectFn)VA(0x51F3D0))((void *)VA(IFACE_RM), NULL, r, rgba, RENDER_SOLID);
}

void game_fill(const short r[4], int color) { game_fill_rgba(r, rgba_of(color)); }

static int g_saved_depth;

void game_text_begin(void)
{
    int *depth = (int *)(*(char **)VA(GRAPHSYS) + GS_DEPTH);
    g_saved_depth = *depth;
    *depth = g_saved_depth + 1;
}

void game_text_end(void) { *(int *)(*(char **)VA(GRAPHSYS) + GS_DEPTH) = g_saved_depth; }

typedef void *(THISCALL *FontGetFn)(void *family, int size, int style);                   /* ret 8 */
typedef void(THISCALL *TextColorFn)(void *buf, const void *c1, const void *c2, int a, int b);  /* ret 0x10 */
typedef int(THISCALL *WriteFn)(void *buf, const void *str, const short *r, int len);       /* ret 0xC */

static int text(void *win, const char *utf8, const short r[4], int align, int color, int size, int style)
{
    static wchar_t wide[512];  /* the wide constructor keeps this pointer while the String lives */
    char *buf = (char *)win + WIN_BUFFER;
    if (!utf8 || !*utf8)
        return r[0];
    void *font = ((FontGetFn)VA(0x522810))((void *)VA(FONTS), size, style);
    if (!font && !*(void **)(buf + BUF_FONT))
        return r[0];
    unsigned char saved[12], saved_colors[BUF_COLORS_N];
    memcpy(saved, buf + BUF_FONT, 12);
    memcpy(saved_colors, buf + BUF_COLORS, BUF_COLORS_N);
    if (font) {
        *(void **)(buf + BUF_FONT) = font;
        *(int *)(buf + BUF_FONT + 4) = 0;
        *(int *)(buf + BUF_FONT + 8) = 0;
    }
    ((TextColorFn)VA(0x50DAF0))(buf, color_of(color), color_of(GC_BLACK), -1, 0);
    GString s;
    const unsigned char *p = (const unsigned char *)utf8;
    while (*p && *p < 0x80)
        p++;
    if (!*p) {
        gs_from_ascii(&s, utf8);
    } else {
        if (MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, 512) <= 0)
            wide[0] = 0;
        gs_from_static(&s, wide);
    }
    int len = *(unsigned short *)((char *)&s + 8), x = r[0];
    if (len) {
        uintptr_t fn = align == GT_RIGHT ? 0x50FA40 : align == GT_CENTRE ? 0x50F850 : 0x50F6E0;
        int got = ((WriteFn)VA(fn))(buf, &s, r, len);
        if (align == GT_RIGHT)
            x = got;
    }
    gs_close(&s);
    memcpy(buf + BUF_FONT, saved, 12);
    memcpy(buf + BUF_COLORS, saved_colors, BUF_COLORS_N);
    return x;
}

int game_text(void *win, const char *utf8, const short r[4], int align, int color, int size)
{
    return text(win, utf8, r, align, color, size, -1);
}

/* PaneModManager::on_redraw 0x6AC889..0x6AC902: FontFamily::get(14, 1), write_cent_l. */
void game_title(void *win, const char *utf8, const short r[4]) { text(win, utf8, r, GT_CENTRE, GC_WHITE, 14, 1); }
