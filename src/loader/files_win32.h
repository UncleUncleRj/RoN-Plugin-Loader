/* The file service's KernelBase hooks (files_win32.c). */
#ifndef RON_FILES_WIN32_H
#define RON_FILES_WIN32_H

/* Hooks CreateFileW, GetFileAttributesW, GetFileAttributesExW, FindFirstFileExW, FindNextFileW and
   FindClose (owner 0); returns how many of the six took. Once per process (tests: fs_win32_forget). */
int fs_win32_install(void);
void fs_win32_forget(void);  /* after hm_reset rolled the hooks back (tests) */

#endif
