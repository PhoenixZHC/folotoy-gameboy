#include "gb_input.h"

gb_keys_t gb_input_map(pad_sample_t sample) {
    gb_keys_t out = {0};
    if (!sample.connected) return out;
    out.right = sample.right && !sample.left;
    out.left = sample.left && !sample.right;
    out.up = sample.up && !sample.down;
    out.down = sample.down && !sample.up;
    out.a = sample.a;
    out.b = sample.b;
    out.select = sample.view;
    out.start = sample.menu;
    return out;
}

void gb_menu_input_init(gb_menu_input_t *state, pad_sample_t sample) {
    *state = (gb_menu_input_t){
        .connected = sample.connected,
        .up = sample.connected && sample.up,
        .down = sample.connected && sample.down,
        .a = sample.connected && sample.a,
        .b = sample.connected && sample.b,
    };
}

gb_menu_action_t gb_menu_input_step(gb_menu_input_t *state, pad_sample_t sample) {
    if (!sample.connected || !state->connected) {
        gb_menu_input_init(state, sample);
        return GB_MENU_NONE;
    }
    gb_menu_action_t action = GB_MENU_NONE;
    if (sample.up && !sample.down && !state->up) action = GB_MENU_UP;
    else if (sample.down && !sample.up && !state->down) action = GB_MENU_DOWN;
    else if (sample.a && !state->a) action = GB_MENU_CONFIRM;
    else if (sample.b && !state->b) action = GB_MENU_BACK;
    gb_menu_input_init(state, sample);
    return action;
}
