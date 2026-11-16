#include "log.h"

#include <orbis/libkernel.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static FILE* g_log_fp = NULL;
static uint64_t g_start_us = 0;
static const char* kLogPath = "/data/pcmanfm/log.txt";

const char* log_file_path(void) {
    return kLogPath;
}

void log_init(void) {
    mkdir("/data/pcmanfm", 0777);
    g_start_us = sceKernelGetProcessTime();
    g_log_fp = fopen(kLogPath, "w");
    if (g_log_fp) {
        fprintf(g_log_fp, "=== PCManFM-PS4 Verbose Log Started ===\n");
        fflush(g_log_fp);
        fsync(fileno(g_log_fp));
    }
    printf("[PCManFM-PS4] Log initialized at %s\n", kLogPath);
}

void log_close(void) {
    if (g_log_fp) {
        log_msg("CORE", "Closing log file cleanly");
        fclose(g_log_fp);
        g_log_fp = NULL;
    }
}

void log_msg(const char* tag, const char* fmt, ...) {
    uint64_t now_us = sceKernelGetProcessTime();
    uint64_t elapsed_us = (now_us >= g_start_us) ? (now_us - g_start_us) : 0;
    uint64_t secs = elapsed_us / 1000000ULL;
    uint64_t usecs = elapsed_us % 1000000ULL;

    char msg_buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg_buf, sizeof(msg_buf), fmt, args);
    va_end(args);

    printf("[%4llu.%06llu][%s] %s\n",
           (unsigned long long)secs,
           (unsigned long long)usecs,
           tag ? tag : "LOG",
           msg_buf);

    if (g_log_fp) {
        fprintf(g_log_fp, "[%4llu.%06llu][%s] %s\n",
                (unsigned long long)secs,
                (unsigned long long)usecs,
                tag ? tag : "LOG",
                msg_buf);
        fflush(g_log_fp);
        fsync(fileno(g_log_fp));
    }
}
