/* A plugin's <Name>.ini: [Plugin] Name, Version, Author, Description, AffectsSim, Folder. */
#ifndef RON_MANIFEST_H
#define RON_MANIFEST_H

#include <stddef.h>

typedef struct Manifest {
    char name[64];          /* shown on the panel; empty = use the plugin id */
    char version[32];
    char author[64];
    char description[256];
    char folder[64];        /* subfolder of plugins\, empty = none */
    int affects_sim;        /* 0 only for "AffectsSim=0" */
    int folder_bad;         /* Folder was given but is not a single folder name */
} Manifest;

/* UTF-8 text (BOM, CRLF or LF). Returns 1 when a [Plugin] section exists. */
int manifest_parse(const char *utf8, size_t len, Manifest *m);

#endif
