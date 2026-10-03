#include <assert.h>
#include <string.h>
#include "bt/uni_bt_le_hid.h"

// Standard button/axis gamepad with a nested Physical collection.
static const uint8_t gamepad[] = {
    0x05,1, 0x09,5, 0xa1,1, 0x09,1, 0xa1,0,
    0x05,9, 0x19,1, 0x29,8, 0x15,0, 0x25,1,
    0x75,1, 0x95,8, 0x81,2,
    0x05,1, 0x09,0x30, 0x09,0x31, 0x15,0x81, 0x25,0x7f,
    0x75,8, 0x95,2, 0x81,2, 0xc0, 0xc0,
};
static const uint8_t keyboard[] = {
    0x05,1, 0x09,6, 0xa1,1, 0x85,1,
    0x05,7, 0x19,0xe0, 0x29,0xe7, 0x15,0, 0x25,1,
    0x75,1, 0x95,8, 0x81,2,
    0x75,8, 0x95,1, 0x81,1,
    0x19,0, 0x29,0x65, 0x15,0, 0x25,0x65,
    0x75,8, 0x95,6, 0x81,0, 0xc0,
};
static const uint8_t consumer[] = {
    0x05,0x0c, 0x09,1, 0xa1,1, 0x85,2,
    0x15,0, 0x26,0xff,3, 0x19,0, 0x2a,0xff,3,
    0x75,0x10, 0x95,1, 0x81,0, 0xc0,
};

int main(void) {
    assert(uni_bt_le_hid_cod(gamepad, sizeof(gamepad)) == 0x0508);
    assert(uni_bt_le_hid_cod(keyboard, sizeof(keyboard)) == 0x0540);
    assert(!uni_bt_le_hid_cod(consumer, sizeof(consumer)));
    uint8_t modified[sizeof(gamepad)];
    memcpy(modified, gamepad, sizeof(gamepad));
    modified[3] = 4;
    assert(uni_bt_le_hid_cod(modified, sizeof(modified)) == 0x0504);
    modified[3] = 2; // Mouse.
    assert(!uni_bt_le_hid_cod(modified, sizeof(modified)));
    modified[1] = 0xff; // Vendor application is not a gamepad.
    modified[3] = 5;
    assert(!uni_bt_le_hid_cod(modified, sizeof(modified)));
    uint8_t composite[sizeof(keyboard) + sizeof(consumer)];
    memcpy(composite, keyboard, sizeof(keyboard));
    memcpy(composite + sizeof(keyboard), consumer, sizeof(consumer));
    assert(uni_bt_le_hid_cod(composite, sizeof(composite)) == 0x0540);
    uint8_t ambiguous[sizeof(keyboard) + sizeof(gamepad)];
    memcpy(ambiguous, keyboard, sizeof(keyboard));
    memcpy(ambiguous + sizeof(keyboard), gamepad, sizeof(gamepad));
    assert(!uni_bt_le_hid_cod(ambiguous, sizeof(ambiguous)));
    for (size_t i = 0; i < sizeof(gamepad); i++)
        assert(!uni_bt_le_hid_cod(gamepad, i));
    const uint8_t push_pop[] = {0x05,1, 0xa4, 0x05,9, 0xb4,
        0x09,5, 0xa1,1, 0x75,8, 0x95,1, 0x81,2, 0xc0};
    assert(uni_bt_le_hid_cod(push_pop, sizeof(push_pop)) == 0x0508);
    const uint8_t extended[] = {0x05,9, 0x0b,5,0,1,0,
        0xa1,1, 0x75,8, 0x95,1, 0x81,2, 0xc0};
    assert(uni_bt_le_hid_cod(extended, sizeof(extended)) == 0x0508);
    const uint8_t output_only[] = {0x05,1, 0x09,5, 0xa1,1, 0x75,8, 0x95,1, 0x91,2, 0xc0};
    assert(!uni_bt_le_hid_cod(output_only, sizeof(output_only)));
    const uint8_t local_reset[] = {0x05,1, 0x09,5, 0x91,2, 0xa1,1, 0x81,2, 0xc0};
    assert(!uni_bt_le_hid_cod(local_reset, sizeof(local_reset)));
    const uint8_t pop_underflow[] = {0xb4};
    assert(!uni_bt_le_hid_cod(pop_underflow, sizeof(pop_underflow)));
    const uint8_t unbalanced[] = {0xc0};
    assert(!uni_bt_le_hid_cod(unbalanced, sizeof(unbalanced)));
    assert(!uni_bt_le_hid_cod(NULL, 8));
    return 0;
}
