#include "applog.h"

#include <stdarg.h>
#include <stdio.h>

#include <switch.h>

#include "config.h"
#include "util.h"

#define LOG_NAME "kaiplayer.log"
#define OLD_NAME "kaiplayer.old.log"
#define LOG_MAX_BYTES (256 * 1024)  // Past this the log of this run stops growing.

static Mutex g_lock;
static bool g_ready;
static long g_written;

static void logPath(char *out, size_t size, const char *name)
{
    snprintf(out, size, "%s/%s", configFolder(), name);
}

void appLogStart(void)
{
    char path[UTIL_PATH_MAX], old[UTIL_PATH_MAX];
    mutexInit(&g_lock);
    logPath(path, sizeof(path), LOG_NAME);
    logPath(old, sizeof(old), OLD_NAME);
    remove(old);
    rename(path, old);
    g_written = 0;
    g_ready = true;
}

void appLog(const char *format, ...)
{
    if (!g_ready)
        return;
    char path[UTIL_PATH_MAX];
    logPath(path, sizeof(path), LOG_NAME);
    mutexLock(&g_lock);
    FILE *f = g_written < LOG_MAX_BYTES ? fopen(path, "a") : NULL;
    if (f) {
        uint64_t ms = armTicksToNs(armGetSystemTick()) / 1000000ull;
        int n = fprintf(f, "%llu.%03llu ", (unsigned long long)(ms / 1000), (unsigned long long)(ms % 1000));
        va_list args;
        va_start(args, format);
        n += vfprintf(f, format, args);
        va_end(args);
        fputc('\n', f);
        fclose(f);
        g_written += n > 0 ? n + 1 : 0;
    }
    mutexUnlock(&g_lock);
}
