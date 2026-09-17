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
