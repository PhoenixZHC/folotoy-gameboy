#include "gb_input.h"

pad_sample_t gb_input_keyboard(const uint8_t *keys, size_t count, uint8_t modifiers) {
    pad_sample_t out = {.connected = true, .any_button = modifiers != 0};
    if (!keys) return out;
    for (size_t i = 0; i < count; i++) {
        // ErrorRollOver / POSTFail / ErrorUndefined are not keys. Release on
        // an invalid report rather than leaving a direction stuck down.
        if (keys[i] >= 1 && keys[i] <= 3)
            return (pad_sample_t){.connected = true};
        out.any_button |= keys[i] != 0;
        switch (keys[i]) {
            case 0x1a: out.up = true; break;    // W
            case 0x04: out.left = true; break;  // A
            case 0x16: out.down = true; break;  // S
            case 0x07: out.right = true; break; // D
            case 0x0d: out.a = true; break;     // J
            case 0x0e: out.b = true; break;     // K
            case 0x0f: out.menu = true; break;  // L = Start
            case 0x0c: out.view = true; break;  // I = Select
            default: break;
        }
    }
    return out;
}

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
