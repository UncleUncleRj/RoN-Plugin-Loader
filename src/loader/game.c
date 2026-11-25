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
