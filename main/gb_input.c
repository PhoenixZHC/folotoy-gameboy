#include "gb_input.h"

static unsigned sample_bits(pad_sample_t p) {
    return (p.right ? 1u : 0) | (p.left ? 2u : 0) |
           (p.up ? 4u : 0) | (p.down ? 8u : 0) |
           (p.a ? 16u : 0) | (p.b ? 32u : 0) |
           (p.view ? 64u : 0) | (p.menu ? 128u : 0);
}

static bool released(pad_sample_t p) {
    return p.connected && !p.any_button && sample_bits(p) == 0;
}

void gb_input_buffer_reset(gb_input_buffer_t *s, pad_sample_t p, bool enabled) {
    s->latest = p;
    s->head = s->count = 0;
    s->enabled = enabled;
    s->armed = enabled && released(p);
}

void gb_input_buffer_push(gb_input_buffer_t *s, pad_sample_t p, int64_t now_us) {
    unsigned previous = sample_bits(s->latest);
    s->latest = p;
    if (!p.connected || !s->enabled) {
        s->head = s->count = 0;
        s->armed = false;
        return;
    }
    if (!s->armed) {
        s->armed = released(p);
        return;
    }
    if (sample_bits(p) == previous) return;
    if (s->count == GB_INPUT_BUFFER_CAPACITY) {
        // Discard the whole backlog, including releases paired with old presses.
        s->dropped += s->count;
        s->head = s->count = 0;
    }
    size_t tail = (s->head + s->count) % GB_INPUT_BUFFER_CAPACITY;
    s->events[tail] = (gb_input_event_t){.sample=p, .at_us=now_us};
    s->count++;
}

pad_sample_t gb_input_buffer_take(gb_input_buffer_t *s, int64_t now_us) {
    if (!s->enabled || !s->armed)
        return (pad_sample_t){.connected=s->latest.connected};
    if (s->count && now_us - s->events[s->head].at_us > GB_INPUT_BUFFER_MAX_AGE_US) {
        // Do not replay stale attacks after a stall; the caller reports drops.
        s->dropped += s->count;
        s->head = s->count = 0;
    }
    if (!s->count) return s->latest;
    pad_sample_t p = s->events[s->head].sample;
    s->head = (s->head + 1) % GB_INPUT_BUFFER_CAPACITY;
    s->count--;
    return p;
}

void gb_input_left_stick(pad_sample_t *sample, int32_t x, int32_t y) {
    if (!sample || !sample->connected) return;
    if (!sample->left && !sample->right) {
        sample->left = x < -160;
        sample->right = x > 160;
    }
    if (!sample->up && !sample->down) {
        sample->up = y < -160;
        sample->down = y > 160;
    }
    sample->any_button |= sample->left || sample->right || sample->up || sample->down;
}

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
