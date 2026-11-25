/* tests.exe has no game: menu.c, menu_draw.c and game.c are left out of it, and this stands in. */
#include "loader/menu.h"

int menu_install(uintptr_t delta, int *frame_hook)
{
    (void)delta;
    *frame_hook = 0;
    return 0;
}

#include "loader/files_game.h"

int fs_game_install(uintptr_t delta)
{
    (void)delta;
    return 0;
}
