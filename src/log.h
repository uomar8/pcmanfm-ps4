#pragma once

#include <stdint.h>

void log_init(void);
void log_close(void);
void log_msg(const char* tag, const char* fmt, ...);
const char* log_file_path(void);
