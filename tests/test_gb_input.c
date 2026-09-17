#include <assert.h>
#include "gb_input.h"

int main(void) {
    pad_sample_t p = {.connected = true, .right = true, .a = true};
    gb_keys_t k = gb_input_map(p);
    assert(k.right && k.a && !k.b && !k.left);
    p.left = true;
    k = gb_input_map(p);
    assert(!k.right && !k.left && k.a);
    p.left = false;
    p.up = true;
    p.view = true;
    p.menu = true;
    k = gb_input_map(p);
    assert(k.right && k.up && k.select && k.start);
    p.connected = false;
    k = gb_input_map(p);
    assert(!k.right && !k.left && !k.up && !k.down);
    assert(!k.a && !k.b && !k.select && !k.start);
    return 0;
}
