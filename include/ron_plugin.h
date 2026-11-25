/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Rj Simas
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
 * associated documentation files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge, publish, distribute,
 * sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
 * NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES
 * OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
/*
 * RoN Plugin Loader: the plugin API, version 1.
 *
 * A plugin is <game>\plugins\<Name>.dll with <game>\plugins\<Name>.ini beside it. It exports one
 * function, RonPluginMain (below), which the loader calls once, on the game's main thread, while
 * plugins load (before the game reads any data). Plugins load top to bottom in the player's load
 * order; when two plugins hook the same function, the lowest plugin's hook runs first.
 *
 * Rules every plugin follows:
 *  - Every address passed in (at, slot, call_site) is a runtime address: convert a static address
 *    from the symbol map with api->va(), or look a function up by its decorated name with
 *    api->sym(). An address that is not committed, readable memory gets RON_E_ARG.
 *  - Hooks and byte patches are accepted only inside RonPluginMain (so every chain follows the load
 *    order), and during RON_EV_LIBRARY for addresses inside the library it announces; event
 *    registrations inside RonPluginMain and during the RON_EV_PRE_INIT event. Later calls return
 *    RON_E_STATE. Nothing is ever unhooked. A plugin may patch over its own earlier byte patches.
 *  - The API is for the game's main thread (RonPluginMain and event callbacks). Plugins must not
 *    call it, or engine code, from their own threads.
 *  - A detour must take its arguments the way the game really passes them, which is not always what
 *    the decorated name says: the compiler gave some functions private conventions. Main::main
 *    takes the HINSTANCE in ECX although its name says __cdecl, and Main::run_game_loop and
 *    Main::init_menus are called with no arguments. Check the function's first instructions and a
 *    caller before writing a detour in C; a C detour keeps no register for the code after it.
 *  - C and C++ plugins alike: the header gives RonPluginMain C linkage and every call __cdecl.
 */
#ifndef RON_PLUGIN_H
#define RON_PLUGIN_H

#include <stdint.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#define RON_EXTERN_C extern "C"
#else
#define RON_EXTERN_C
#endif

#define RON_API_MAJOR 1
#define RON_API_MINOR 1   /* 1: files (file_use, file_folder, file_generate, file_path) and RON_EV_LIBRARY */

typedef struct RonPlugin RonPlugin;   /* opaque, one per plugin */

typedef enum { RON_OK = 0, RON_E_ARG, RON_E_SIG, RON_E_CONFLICT, RON_E_STATE, RON_E_MEM } RonResult;
typedef enum { RON_EXE_UNKNOWN = 0, RON_EXE_STOCK, RON_EXE_PATCHED } RonExeKind;
/* RON_EV_RENDER_FRAME fires for every drawn picture of a match (from its loading screen on), not in
   the menus.
   RON_EV_LIBRARY (minor 1) fires once for each library the game loads after plugins: the renderer
   (d3dgl.dll, inside GraphSys::init_system) and the sound library (dssl.dll, SoundSys::init_system),
   with game_object = its HMODULE. During the callback the hook calls (hook_inline, hook_call,
   hook_ptr, patch_bytes) accept addresses inside that library's image, in load order, as they do in
   RonPluginMain for everything else. Check the library's own TimeDateStamp before hooking it. */
typedef enum { RON_EV_PRE_INIT, RON_EV_POST_INIT, RON_EV_MATCH_BUILT, RON_EV_SIM_FRAME,
               RON_EV_RENDER_FRAME, RON_EV_MATCH_END, RON_EV_SHUTDOWN, RON_EV_LIBRARY, RON_EV_COUNT } RonEvent;

typedef struct RonGameInfo {
    RonExeKind exe;          /* stock riseofnations.exe, or a patched copy of the same build with an extra code section */
    uint32_t timestamp;      /* the exe's PE TimeDateStamp, 0x6674863F */
    uintptr_t delta;         /* runtime image base - 0x400000 (ASLR) */
    const wchar_t *game_dir; /* the game folder, no trailing backslash */
} RonGameInfo;

/* game_object: the Game or Setup object for game events (this pointer of the hooked function),
   NULL for pre_init, post_init and shutdown. */
typedef void(__cdecl *RonEventFn)(RonEvent ev, void *game_object, void *user);

/* A file generator (minor 1). in_path is the file that would win without this plugin (the plugins
   above it, else the game's mods, else the base install); the generator writes its own version to
   out_path (its folders exist) and returns nonzero, or returns 0 to leave the file as it was. It runs
   once per start, the first time the game asks for the file, on whichever thread asks: it may only
   read and write files, never call engine code. Generators of files the simulation reads must give
   the same output on every machine (no clock, no random numbers, no local settings). */
typedef int(__cdecl *RonFileGenFn)(const char *game_rel, const wchar_t *in_path, const wchar_t *out_path,
                                   void *user);
typedef struct RonApi {
    uint32_t major, minor;
    /* One line in <game>\plugins\loader.log, tagged with the plugin's name. */
    void(__cdecl *log)(RonPlugin *self, const char *fmt, ...);
    const RonGameInfo *(__cdecl *game)(void);
    uintptr_t(__cdecl *va)(uintptr_t static_va);                 /* + delta */
    uintptr_t(__cdecl *sym)(const char *decorated_name);         /* runtime address, 0 if unknown */
    /* Inline hook: checks sig_len bytes at `at` against `sig` (against the stock bytes when the
       address is already hooked). `mask` may be NULL (every byte significant); a 0x00 mask byte
       skips that byte, for absolute addresses that ASLR relocates. stolen <= sig_len, stolen >= 5,
       and the stolen bytes must hold no relative branch or call. *orig receives what the detour
       calls to go on (the next hook up the chain, or the game's code). */
    RonResult(__cdecl *hook_inline)(RonPlugin *self, uintptr_t at, const uint8_t *sig, const uint8_t *mask,
                                    uint32_t sig_len, uint32_t stolen, void *detour, void **orig);
    /* Call-site hook: `call_site` is an E8 rel32 call; *orig receives its previous target. */
    RonResult(__cdecl *hook_call)(RonPlugin *self, uintptr_t call_site, void *detour, void **orig);
    /* Pointer-slot hook (vtable entry, import address table entry). expected 0 = don't check. */
    RonResult(__cdecl *hook_ptr)(RonPlugin *self, uintptr_t slot, uintptr_t expected, void *detour, void **orig);
    /* Byte patch: writes `bytes` over `len` bytes at `at` when they equal `expect`. */
    RonResult(__cdecl *patch_bytes)(RonPlugin *self, uintptr_t at, const uint8_t *expect, const uint8_t *bytes,
                                    uint32_t len);
    RonResult(__cdecl *on_event)(RonPlugin *self, RonEvent ev, RonEventFn fn, void *user);
    const wchar_t *(__cdecl *folder)(RonPlugin *self);          /* <game>\plugins\<Folder>, NULL without one */

    /* ---- minor 1: files. Check api->minor >= 1 before using these. ----
     * game_rel is a path relative to the game folder as the game asks for it ("Data\\rules.xml",
     * "art\\x.tga", "masks.txt"); case and / or \ don't matter. Programs and libraries (.dll, .exe,
     * ...) and anything under plugins\ or mods\ are never served (RON_E_ARG). The plugin's ini Folder
     * is served without any call, as if file_folder had been called on it first. When two plugins
     * serve one name, the lower one in the load order wins; a plugin's own later call wins over its
     * earlier ones. Files are served to the game's own lookup (String::prepend_content_dir, so a
     * plugin wins over the game's mods) and to every read of <game>\<game_rel> in the process
     * (CreateFileW, attribute queries, FindFirstFileExW); folder listings show a plugin's added files
     * and folders. Calls are accepted inside RonPluginMain and during RON_EV_PRE_INIT. */
    /* Serve `path` for game_rel. A relative path is taken inside the plugin's Folder. */
    RonResult(__cdecl *file_use)(RonPlugin *self, const char *game_rel, const wchar_t *path);
    /* Serve every file in this folder at its path relative to it (laid out like the game folder). */
    RonResult(__cdecl *file_folder)(RonPlugin *self, const wchar_t *folder);
    /* Make game_rel with fn when it is first asked for (see RonFileGenFn). */
    RonResult(__cdecl *file_generate)(RonPlugin *self, const char *game_rel, RonFileGenFn fn, void *user);
    /* The file the game reads for game_rel after every plugin, or the base install's copy when no
       plugin serves it (the game's own mods are not consulted). Returns 0 when it does not fit. */
    int(__cdecl *file_path)(const char *game_rel, wchar_t *out, uint32_t cap);
} RonApi;

typedef struct RonPluginInfo {
    uint32_t api_major;   /* set to RON_API_MAJOR */
    int affects_sim;      /* 1 when the plugin changes anything the simulation reads */
} RonPluginInfo;

/* Each plugin exports:
 *   RON_PLUGIN_EXPORT int __cdecl RonPluginMain(const RonApi *api, RonPlugin *self, RonPluginInfo *out);
 * Return nonzero to stay loaded. Returning 0 (or a wrong api_major) unloads the plugin and rolls
 * back the hooks and patches it made. *orig is valid as soon as a hook call returns RON_OK. */
#define RON_PLUGIN_EXPORT RON_EXTERN_C __declspec(dllexport)
typedef int(__cdecl *RonPluginMainFn)(const RonApi *api, RonPlugin *self, RonPluginInfo *out);

#ifdef __cplusplus
}
#endif

#endif
