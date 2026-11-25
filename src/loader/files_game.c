/*
 * The file service in the game's own lookup (design section 9). Before the game opens a file it
 * passes the name through String::prepend_content_dir (0xA1D690; 60 calls in 36 functions, among them
 * File::open, XML::init, Text::open, Filemap::open and ImageIO::load), which asks the game's mods
 * where the file comes from (Atlas: Mods). This hook lets the game answer first, then puts a plugin's
 * file in its place, so plugins win over the game's mods, and a generator receives what the mods
 * gave. Readers that skip this function are caught by files_win32.c.
 */
#include <windows.h>
#include "loader/files.h"
#include "loader/files_game.h"
#include "loader/game.h"
#include "loader/hooks.h"
#include "loader/log.h"

#define THISCALL __attribute__((thiscall))
#define PREPEND_VA 0xA1D690u

/* thiscall(src String, hidden return String *, int force, char *file, int line), ret 0x10, returns
   the return String (ledger menu F6). */
typedef void *(THISCALL *PrependFn)(void *src, void *ret, int force, const char *file, int line);
static PrependFn o_prepend;

static void *THISCALL d_prepend(void *src, void *ret, int force, const char *file, int line)
{
    /* the name first: the caller may have the result written over its source */
    wchar_t name[FS_MAX_REL], rel[FS_MAX_REL], base[MAX_PATH];
    int mine = fs_active() && gs_text(src, name, FS_MAX_REL) && fs_norm(name, rel, FS_MAX_REL);
    void *r = o_prepend(src, ret, force, file, line);
    if (!mine)
        return r;
    if (!gs_text(r, base, MAX_PATH))
        base[0] = 0;
    const wchar_t *path = fs_resolve(rel, base);
    if (path) {
        /* the wide constructor keeps the pointer; fs_resolve's paths live as long as the process */
        gs_close((GString *)r);
        gs_from_static((GString *)r, path);
    }
    return r;
}

int fs_game_install(uintptr_t delta)
{
    /* push ebp; mov ebp,esp; push -1; push <handler> (absolute: ASLR moves it) */
    static const uint8_t sig[10] = {0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0, 0, 0, 0};
    static const uint8_t mask[10] = {1, 1, 1, 1, 1, 1, 0, 0, 0, 0};
    game_init(delta);
    RonResult r = hm_inline(0, PREPEND_VA + delta, sig, mask, 10, 5, (void *)d_prepend, (void **)&o_prepend);
    if (r != RON_OK)
        log_line("loader", "files: String::prepend_content_dir not hooked (%d); plugins serve files only "
                           "where the game's mods have none", (int)r);
    return r == RON_OK;
}
