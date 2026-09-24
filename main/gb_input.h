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

typedef enum {
    GB_MENU_NONE,
    GB_MENU_UP,
    GB_MENU_DOWN,
    GB_MENU_CONFIRM,
    GB_MENU_BACK,
} gb_menu_action_t;

typedef struct {
    bool connected;
    bool up, down, a, b;
} gb_menu_input_t;

void gb_menu_input_init(gb_menu_input_t *state, pad_sample_t sample);
gb_menu_action_t gb_menu_input_step(gb_menu_input_t *state, pad_sample_t sample);
