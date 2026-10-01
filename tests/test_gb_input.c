#include <assert.h>
#include "gb_input.h"

static void test_keyboard(void) {
    const uint8_t usages[] = {0x1a, 0x04, 0x16, 0x07, 0x0d, 0x0e, 0x0f, 0x0c};
    for (size_t i = 0; i < sizeof(usages); i++) {
        pad_sample_t p = gb_input_keyboard(&usages[i], 1, 0);
        gb_keys_t k = gb_input_map(p);
        const bool pressed[] = {k.up, k.left, k.down, k.right, k.a, k.b, k.start, k.select};
        assert(p.connected && p.any_button);
        for (size_t j = 0; j < sizeof(pressed) / sizeof(pressed[0]); j++)
            assert(pressed[j] == (i == j));
    }
    // A complete report carries all held keys; repeated reports remain held.
    const uint8_t chord[] = {0x07, 0x0d, 0x0e, 0, 0, 0};
    for (int i = 0; i < 3; i++) {
        gb_keys_t k = gb_input_map(gb_input_keyboard(chord, sizeof(chord), 2));
        assert(k.right && k.a && k.b && !k.left && !k.start);
    }
    const uint8_t partial_release[] = {0x07, 0, 0, 0, 0, 0};
    pad_sample_t p = gb_input_keyboard(partial_release, sizeof(partial_release), 0);
    assert(p.right && !p.a && !p.b);
    const uint8_t released[6] = {0};
    p = gb_input_keyboard(released, sizeof(released), 0);
    assert(p.connected && !p.any_button && !p.right && !p.a);
    p = gb_input_keyboard(usages, sizeof(usages), 0xff);
    gb_keys_t k = gb_input_map(p);
    assert(!k.up && !k.down && !k.left && !k.right);
    assert(k.a && k.b && k.start && k.select);
    p.connected = false;
    k = gb_input_map(p);
    assert(!k.a && !k.b && !k.start && !k.select);
    for (uint8_t error = 1; error <= 3; error++) {
        uint8_t invalid[] = {0x07, error, 0x0d};
        p = gb_input_keyboard(invalid, sizeof(invalid), 2);
        assert(p.connected && !p.any_button && !p.right && !p.a);
    }
    const uint8_t unmapped[] = {0x28}; // Enter may wake but is not Start.
    p = gb_input_keyboard(unmapped, sizeof(unmapped), 0);
    assert(p.any_button && !p.menu && !p.a && !p.b);
    p = gb_input_keyboard(NULL, 0, 2);
    assert(p.connected && p.any_button && !p.up);
    gb_menu_input_t menu;
    gb_menu_input_init(&menu, gb_input_keyboard(released, sizeof(released), 0));
    p = gb_input_keyboard(&usages[4], 1, 0); // J confirms once, not once per repeat.
    assert(gb_menu_input_step(&menu, p) == GB_MENU_CONFIRM);
    assert(gb_menu_input_step(&menu, p) == GB_MENU_NONE);
    p = gb_input_keyboard(&usages[5], 1, 0);
    assert(gb_menu_input_step(&menu, p) == GB_MENU_BACK);
}

int main(void) {
    test_keyboard();
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
