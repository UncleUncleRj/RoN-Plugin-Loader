#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include "loader/log.h"

static FILE *g_log;
static SRWLOCK g_lock = SRWLOCK_INIT;

int log_open(const wchar_t *path)
{
    AcquireSRWLockExclusive(&g_lock);
    if (g_log)
        fclose(g_log);
    g_log = _wfopen(path, L"wb");
    int ok = g_log != NULL;
    ReleaseSRWLockExclusive(&g_lock);
    return ok;
}

void log_line(const char *owner, const char *fmt, ...)
{
    char text[1024];
    char line[1100];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    SYSTEMTIME t;
    GetLocalTime(&t);
    int n = snprintf(line, sizeof line, "%02u:%02u:%02u.%03u [%s] %s\r\n", t.wHour, t.wMinute, t.wSecond,
                     t.wMilliseconds, owner ? owner : "?", text);
    if (n < 0)
        return;
    if (n >= (int)sizeof line)
        n = (int)sizeof line - 1;
    OutputDebugStringA(line);
    AcquireSRWLockExclusive(&g_lock);
    if (g_log) {
        fwrite(line, 1, (size_t)n, g_log);
        fflush(g_log);
    }
    ReleaseSRWLockExclusive(&g_lock);
}

void log_close(void)
{
    AcquireSRWLockExclusive(&g_lock);
    if (g_log)
        fclose(g_log);
    g_log = NULL;
    ReleaseSRWLockExclusive(&g_lock);
}
