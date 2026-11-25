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
 *    order); event registrations inside RonPluginMain and during the RON_EV_PRE_INIT event. Later
 *    calls return RON_E_STATE. Nothing is ever unhooked.
 *  - The API is for the game's main thread (RonPluginMain and event callbacks). Plugins must not
 *    call it, or engine code, from their own threads.
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
#define RON_API_MINOR 0

typedef struct RonPlugin RonPlugin;   /* opaque, one per plugin */

typedef enum { RON_OK = 0, RON_E_ARG, RON_E_SIG, RON_E_CONFLICT, RON_E_STATE, RON_E_MEM } RonResult;
typedef enum { RON_EXE_UNKNOWN = 0, RON_EXE_STOCK, RON_EXE_CBP } RonExeKind;
typedef enum { RON_EV_PRE_INIT, RON_EV_POST_INIT, RON_EV_MATCH_BUILT, RON_EV_SIM_FRAME,
               RON_EV_RENDER_FRAME, RON_EV_MATCH_END, RON_EV_SHUTDOWN, RON_EV_COUNT } RonEvent;

typedef struct RonGameInfo {
    RonExeKind exe;          /* stock riseofnations.exe or CBP's riseofnations_CBP.exe */
    uint32_t timestamp;      /* the exe's PE TimeDateStamp, 0x6674863F */
    uintptr_t delta;         /* runtime image base - 0x400000 (ASLR) */
    const wchar_t *game_dir; /* the game folder, no trailing backslash */
} RonGameInfo;

/* game_object: the Game or Setup object for game events (this pointer of the hooked function),
   NULL for pre_init, post_init and shutdown. */
typedef void(__cdecl *RonEventFn)(RonEvent ev, void *game_object, void *user);

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
