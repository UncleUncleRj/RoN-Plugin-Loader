/*
 * SplashTest1 and SplashTest2 (this file, built twice with SPLASH_N = 1 and 2): test plugins for the
 * load order. Each replaces the first intro video, the SkyBox logo (skybox_intro_1920.wmv, or
 * skybox_intro_1440.wmv on a squarer screen), with its own 3-second picture saying "Plugin N
 * Success". Both hook the same function, so the plugin lower in the load order, which loads later,
 * runs first and wins; the other one then sees a file name it does not replace.
 *
 * How the game plays its intros: System::init calls Video::init (0x53FCB0, thiscall(this, width,
 * height), ret 8) before each video. Video::init asks the renderer, d3dgl.dll, for a player object
 * and keeps it at Video+4; the game then calls the player's load(String const &name, int) through
 * its virtual table (slot +0x10, D3D11GLVideo::load), which hands the name unchanged to DirectShow
 * (at most 259 characters). d3dgl.dll is loaded after plugins are, so a plugin cannot hook load
 * through the API; this one hooks Video::init and gives each new player its own copy of the
 * virtual table with load replaced. The copy keeps the entry before the table (the RTTI pointer).
 *
 * The intros do not play when the game starts with +skipIntro (the game's own restart adds it, and
 * so Apply and Restart does), when rise2.ini has SkipIntroMovies=1, or after a click or key press
 * during an earlier intro.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "ron_plugin.h"

#ifndef SPLASH_N
#error "build with -DSPLASH_N=1 or -DSPLASH_N=2"
#endif

#define THISCALL  __attribute__((thiscall))
#define VT_COPY   32  /* entries copied, from the one before the table; D3D11GLVideo's has 19 */
#define SLOT_LOAD 4   /* +0x10 */
#define PATH_MAX_D3DGL 0x104

static const RonApi *g_api;
static RonPlugin *g_self;

/* [0] for skybox_intro_1920.wmv, [1] for skybox_intro_1440.wmv */
static const wchar_t *const k_stock[2] = {L"skybox_intro_1920.wmv", L"skybox_intro_1440.wmv"};
static const wchar_t *const k_ours[2] = {L"splash_1920.wmv", L"splash_1440.wmv"};
static wchar_t g_path[2][MAX_PATH];  /* lives as long as the plugin: the game's String keeps the pointer */
static int g_have[2];

typedef struct {
    unsigned char b[0x14];
} __attribute__((aligned(4))) GameString;  /* the game's String, built only by its constructors */

typedef int(THISCALL *VideoInitFn)(void *video, int width, int height);
typedef int(THISCALL *LoadFn)(void *player, const GameString *name, int flag);
typedef void(THISCALL *StrCtorW)(GameString *s, const wchar_t *w);   /* String::String(wchar_t const *) 0xA1EDD0 */
typedef void(THISCALL *StrClose)(GameString *s);                     /* String::close 0xA1CF40 */
typedef const wchar_t *(THISCALL *StrChars)(const GameString *s);    /* String::str 0xA1F0A0 */

static VideoInitFn o_video_init;
static LoadFn o_load;
static void *g_vt[VT_COPY];

static void to_utf8(const wchar_t *w, char *out, int cap)
{
    if (WideCharToMultiByte(CP_UTF8, 0, w, -1, out, cap, NULL, NULL) <= 0)
        out[0] = 0;
}

/* The file name part of the String's text, copied into out; 0 when empty or too long. */
static int name_of(const GameString *s, wchar_t *out, int cap)
{
    out[0] = 0;
    if (!s || !*(void *const *)s)
        return 0;
    int len = *(const unsigned short *)(s->b + 8);
    if (len <= 0 || len >= cap)
        return 0;
    const wchar_t *w = ((StrChars)g_api->va(0xA1F0A0))(s);
    if (!w)
        return 0;
    int start = 0;
    for (int i = 0; i < len; i++)
        if (w[i] == L'\\' || w[i] == L'/')
            start = i + 1;
    memcpy(out, w + start, (size_t)(len - start) * sizeof(wchar_t));
    out[len - start] = 0;
    return 1;
}

/* The player's load: our video in place of the SkyBox intro, everything else unchanged. */
static int THISCALL d_load(void *player, const GameString *name, int flag)
{
    wchar_t file[MAX_PATH];
    if (name_of(name, file, MAX_PATH)) {
        for (int i = 0; i < 2; i++) {
            if (!g_have[i] || _wcsicmp(file, k_stock[i]) != 0)
                continue;
            GameString s;
            ((StrCtorW)g_api->va(0xA1EDD0))(&s, g_path[i]);
            int r = o_load(player, &s, flag);
            ((StrClose)g_api->va(0xA1CF40))(&s);
            char a[MAX_PATH * 3], b[MAX_PATH * 3];
            to_utf8(file, a, sizeof a);
            to_utf8(g_path[i], b, sizeof b);
            g_api->log(g_self, "intro %s plays as %s (load returned %d)", a, b, r);
            return r;
        }
    }
    return o_load(player, name, flag);
}

/* Video::init: the original makes the player (Video+4); then that player gets our table copy. */
static int THISCALL d_video_init(void *video, int width, int height)
{
    int r = o_video_init(video, width, height);
    void ***player = *(void ****)((char *)video + 4);
    if (player && *player && *player != g_vt + 1) {
        memcpy(g_vt, *player - 1, sizeof g_vt);
        o_load = (LoadFn)g_vt[1 + SLOT_LOAD];
        g_vt[1 + SLOT_LOAD] = (void *)d_load;
        *player = g_vt + 1;
    }
    return r;
}

RON_PLUGIN_EXPORT int __cdecl RonPluginMain(const RonApi *api, RonPlugin *self, RonPluginInfo *out)
{
    g_api = api;
    g_self = self;
    out->api_major = RON_API_MAJOR;
    out->affects_sim = 0;

    const wchar_t *dir = api->folder(self);
    if (!dir) {
        api->log(self, "the ini names no Folder, so there is no video to show");
        return 0;
    }
    int any = 0;
    for (int i = 0; i < 2; i++) {
        int n = swprintf(g_path[i], MAX_PATH, L"%ls\\%ls", dir, k_ours[i]);
        g_have[i] = n > 0 && n < PATH_MAX_D3DGL && GetFileAttributesW(g_path[i]) != INVALID_FILE_ATTRIBUTES;
        if (!g_have[i]) {
            char p[MAX_PATH * 3];
            to_utf8(g_path[i], p, sizeof p);
            api->log(self, "missing or path too long: %s", p);
        }
        any |= g_have[i];
    }
    if (!any)
        return 0;

    /* Video::init: push ebp; mov ebp, esp; cmp dword ptr [ebp+0xC], 0; push esi; mov esi, ecx */
    static const uint8_t sig[] = {0x55, 0x8B, 0xEC, 0x83, 0x7D, 0x0C, 0x00, 0x56, 0x8B, 0xF1};
    RonResult r = api->hook_inline(self, api->va(0x53FCB0), sig, NULL, sizeof sig, 7, (void *)d_video_init,
                                   (void **)&o_video_init);
    if (r != RON_OK) {
        api->log(self, "could not hook Video::init (%d)", (int)r);
        return 0;
    }
    api->log(self, "ready: the SkyBox intro will say \"Plugin %d Success\"", SPLASH_N);
    return 1;
}
