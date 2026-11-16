#pragma once

#include <stddef.h>
#include <stdint.h>

enum class ImePollState {
    Idle = 0,
    Running,
    CompletedOk,
    Canceled,
    Failed
};

bool ps4_ime_init_system(void);
bool ps4_ime_open(const char* title, const char* initial_text, size_t max_chars);
ImePollState ps4_ime_poll(char* out_utf8, size_t out_size);
void ps4_ime_close(void);
