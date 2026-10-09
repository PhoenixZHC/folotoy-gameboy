#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
// Merge normalized left-stick directions, with a dead zone and D-pad priority.
void gb_input_left_stick(pad_sample_t *sample, int32_t x, int32_t y);
// HID Keyboard/Keypad usage IDs, independent of case and host input methods.
pad_sample_t gb_input_keyboard(const uint8_t *keys, size_t count, uint8_t modifiers);

// Queue complete states for gameplay; menus continue to read the live state.
#define GB_INPUT_BUFFER_CAPACITY 16
#define GB_INPUT_BUFFER_MAX_AGE_US 100000
typedef struct {
    pad_sample_t sample;
    int64_t at_us;
} gb_input_event_t;
typedef struct {
    pad_sample_t latest;
    gb_input_event_t events[GB_INPUT_BUFFER_CAPACITY];
    size_t head, count;
    bool enabled, armed;
    uint32_t dropped;
} gb_input_buffer_t;
void gb_input_buffer_reset(gb_input_buffer_t *state, pad_sample_t sample, bool enabled);
void gb_input_buffer_push(gb_input_buffer_t *state, pad_sample_t sample, int64_t now_us);
pad_sample_t gb_input_buffer_take(gb_input_buffer_t *state, int64_t now_us);

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
