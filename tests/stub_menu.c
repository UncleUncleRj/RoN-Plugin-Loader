/* tests.exe has no game: menu.c, menu_draw.c and game.c are left out of it, and this stands in. */
#include "loader/menu.h"

int menu_install(uintptr_t delta, int *frame_hook)
{
    (void)delta;
    *frame_hook = 0;
    return 0;
}
