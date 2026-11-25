/* The file service's hook on the game's own file lookup (files_game.c; needs the game). */
#ifndef RON_FILES_GAME_H
#define RON_FILES_GAME_H

#include <stdint.h>

int fs_game_install(uintptr_t delta);  /* hooks String::prepend_content_dir; 1 when it took */

#endif
