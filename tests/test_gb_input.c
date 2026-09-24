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

    gb_menu_input_t menu;
    p = (pad_sample_t){.connected = true, .a = true};
    gb_menu_input_init(&menu, p);
    assert(gb_menu_input_step(&menu, p) == GB_MENU_NONE);
    p.a = false;
    assert(gb_menu_input_step(&menu, p) == GB_MENU_NONE);
    p.up = true;
    assert(gb_menu_input_step(&menu, p) == GB_MENU_UP);
    assert(gb_menu_input_step(&menu, p) == GB_MENU_NONE);
    p.up = false;
    p.down = true;
    assert(gb_menu_input_step(&menu, p) == GB_MENU_DOWN);
    p.down = false;
    p.a = true;
    assert(gb_menu_input_step(&menu, p) == GB_MENU_CONFIRM);
    p.a = false;
    p.b = true;
    assert(gb_menu_input_step(&menu, p) == GB_MENU_BACK);
    p.connected = false;
    assert(gb_menu_input_step(&menu, p) == GB_MENU_NONE);
    p.connected = true;
    assert(gb_menu_input_step(&menu, p) == GB_MENU_NONE);
    p.b = false;
    assert(gb_menu_input_step(&menu, p) == GB_MENU_NONE);
    p.b = true;
    assert(gb_menu_input_step(&menu, p) == GB_MENU_BACK);
    return 0;
}
