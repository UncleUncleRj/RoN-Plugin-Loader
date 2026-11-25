/* The file service (design section 9): which plugin serves a game file, and where it comes from.
 * Owner 0 is the loader, owners 1..n are plugins in load order; when two owners serve one name the
 * higher owner (lower in the load order) wins, and an owner's later claim wins over its earlier one.
 * Every function is safe to call from any thread. */
#ifndef RON_FILES_H
#define RON_FILES_H

#include <stdint.h>
#include <wchar.h>
#include "ron_plugin.h"

#define FS_MAX_REL 260

/* game_dir: the game folder (no trailing backslash); gen_dir: the folder generated files go to, one
   subfolder per plugin, emptied here. */
void fs_init(const wchar_t *game_dir, const wchar_t *gen_dir);
void fs_reset(void);  /* forget everything (tests) */
void fs_lock(void);   /* afterwards registrations return RON_E_STATE */

/* A name the game asks for, relative to the game folder ("Data/rules.xml", ".\\art\\x.tga"), in the
   service's form ("Data\\rules.xml": backslashes, no leading .\); 0 when it may never be served:
   absolute or empty names, "." or ".." parts, anything under plugins\ or mods\, programs and libraries
   (.dll .exe .asi .sys .ocx .cpl .drv .scr .com). */
int fs_norm(const wchar_t *name, wchar_t *rel, int cap);
/* A path (relative to the current folder, or absolute) inside the game folder: its fs_norm form, or 0. */
int fs_rel_of_path(const wchar_t *path, wchar_t *rel, int cap);

RonResult fs_use(int owner, const wchar_t *rel_name, const wchar_t *path);
/* Every file under `folder` at its path relative to it. Returns the number of files it serves via
   *count when count is not NULL. */
RonResult fs_folder(int owner, const wchar_t *folder, int *count);
/* owner_id names the plugin's subfolder of gen_dir. */
RonResult fs_generate(int owner, const wchar_t *owner_id, const wchar_t *rel_name, RonFileGenFn fn, void *user);
void fs_drop_owner(int owner);

int fs_active(void);  /* 1 once anything is served (the hooks skip all work before that) */
/* The path served for rel (fs_norm form), or NULL when no plugin serves it. `base` is the path the
   game would use without plugins (after its own mods; relative to the game folder or absolute), NULL
   for the base install's copy; only generators read it. The result lives as long as the service. */
const wchar_t *fs_resolve(const wchar_t *rel, const wchar_t *base);
/* The plugin folders (roots) that hold the folder reldir (fs_norm form, "" never counts), in load
   order, top first; returns how many, at most max. */
int fs_dir_roots(const wchar_t *reldir, const wchar_t **roots, int max);
int fs_owner_files(int owner);  /* how many names the owner serves */

#endif
