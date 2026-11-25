/* <game>\plugins\loader.log: one UTF-8 line per call, "HH:MM:SS.mmm [owner] text". */
#ifndef RON_LOG_H
#define RON_LOG_H

#include <wchar.h>

int log_open(const wchar_t *path);   /* truncates; 1 on success */
void log_line(const char *owner, const char *fmt, ...);
void log_close(void);

#endif
