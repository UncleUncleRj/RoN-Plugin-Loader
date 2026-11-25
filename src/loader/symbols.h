/* Function and data addresses by decorated name, from the game's own sbl\rise_z.map. */
#ifndef RON_SYMBOLS_H
#define RON_SYMBOLS_H

#include <stdint.h>
#include <wchar.h>

/* Indexes the map. Returns the number of names, or -1 when the file is missing or its
   "Timestamp is xxxxxxxx" header is not exe_timestamp (every lookup then returns 0). */
int sym_load(const wchar_t *map_path, uint32_t exe_timestamp);

/* The map's address (static, before the ASLR delta) of a decorated name; 0 when unknown. */
uint32_t sym_static(const char *decorated);

void sym_free(void);

#endif
