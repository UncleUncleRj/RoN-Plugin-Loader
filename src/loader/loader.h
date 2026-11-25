/* The loading sequence: find the plugins, settle the load order, load them, fire pre_init. */
#ifndef RON_LOADER_H
#define RON_LOADER_H

#include <wchar.h>
#include "ron_plugin.h"
#include "loader/manifest.h"
#include "loader/order.h"

typedef enum { LP_DISABLED, LP_LOADED, LP_FAILED } LpState;

typedef struct LoaderPlugin {
    wchar_t id[64];       /* the DLL's file name without .dll */
    Manifest m;           /* its ini; m.name is the id when the ini has no Name */
    int enabled, is_new;
    LpState state;
    char reason[128];     /* why it failed */
} LoaderPlugin;

typedef struct LoaderConfig {
    RonGameInfo game;
    int install_game_hooks;
} LoaderConfig;

int loader_run(const LoaderConfig *cfg);   /* returns the number of plugins loaded */
const LoaderPlugin *loader_plugins(int *count);

/* Called by the bootstrap from the hook at Main::main, on the game's main thread. */
__declspec(dllexport) void __cdecl RonLoaderStart(void);

/* ---- shared with api.c ---- */
struct RonPlugin {
    int owner;            /* 1-based position in the load order */
};
const RonApi *api_table(void);
const RonGameInfo *loader_game(void);
const char *loader_owner_name(int owner);       /* "loader" for owner 0 */
const wchar_t *loader_owner_folder(int owner);  /* <game>\plugins\<Folder> or NULL */
void api_notify(int owner, int other, uintptr_t at, int kind);

#endif
