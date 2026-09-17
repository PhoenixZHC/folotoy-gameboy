#pragma once

// SUMI 核心的最小 ESP-IDF 兼容层，不引入 Arduino runtime。
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_attr.h"
#include "esp_timer.h"

inline unsigned long millis(void) {
    return (unsigned long)(esp_timer_get_time() / 1000);
}

struct SerialCompat {
    template <typename... Args>
    void printf(const char *format, Args... args) const { ::printf(format, args...); }
    void println(const char *message) const { ::puts(message); }
};

inline const SerialCompat Serial;
