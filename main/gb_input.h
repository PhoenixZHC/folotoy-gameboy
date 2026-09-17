#pragma once

#include <stdbool.h>

typedef struct {
    bool connected;
    bool right, left, up, down;
    bool a, b, view, menu;
    bool any_button; // All reported buttons, including those not mapped to GB.
} pad_sample_t;

typedef struct {
    bool right, left, up, down;
    bool a, b, select, start;
} gb_keys_t;

gb_keys_t gb_input_map(pad_sample_t sample);
