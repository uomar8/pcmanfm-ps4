#include "log.h"

#include <fcntl.h>
#include <orbis/libkernel.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int g_log_fd = -1;
static uint64_t g_start_us = 0;
static const char* kLogPath = "/data/pcmanfm/log.txt";

const char* log_file_path(void) {
    return kLogPath;
}

void log_init(void) {
    mkdir("/data/pcmanfm", 0777);
    g_start_us = sceKernelGetProcessTime();
    g_log_fd = open(kLogPath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (g_log_fd >= 0) {
        const char* hdr = "=== PCManFM-PS4 Verbose Log Started ===\n";
        write(g_log_fd, hdr, strlen(hdr));
        fsync(g_log_fd);
    }
    printf("[PCManFM-PS4] Log initialized at %s (fd=%d)\n", kLogPath, g_log_fd);
}

void log_close(void) {
    if (g_log_fd >= 0) {
        log_msg("CORE", "Closing log file cleanly");
        fsync(g_log_fd);
        close(g_log_fd);
        g_log_fd = -1;
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

    char line_buf[1280];
    int n = snprintf(line_buf, sizeof(line_buf), "[%4llu.%06llu][%s] %s\n",
                     (unsigned long long)secs,
                     (unsigned long long)usecs,
                     tag ? tag : "LOG",
                     msg_buf);

    printf("%s", line_buf);

    if (g_log_fd >= 0 && n > 0) {
        size_t wlen = (size_t)n < sizeof(line_buf) ? (size_t)n : sizeof(line_buf) - 1;
        write(g_log_fd, line_buf, wlen);
        fsync(g_log_fd);
    }
}
