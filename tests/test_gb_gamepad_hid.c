#include <assert.h>
#include <string.h>
#include "uni_hid_device.h"
#include "parser/uni_hid_parser_standard.h"
#include "parser/uni_hid_parser_android.h"

bool uni_hid_device_set_ready_complete(uni_hid_device_t* d) { (void)d; return true; }
void uni_hid_device_disconnect(uni_hid_device_t* d) { (void)d; }
void uni_hid_device_delete(uni_hid_device_t* d) { (void)d; }

// Buttons, stick and consumer keys deliberately use different report IDs.
static uint8_t descriptor[] = {
    0x05,1, 0x09,5, 0xa1,1,
    0x85,1, 0x05,9, 0x19,1, 0x29,8, 0x15,0, 0x25,1,
    0x75,1, 0x95,8, 0x81,2,
    0x85,2, 0x05,1, 0x09,0x30, 0x09,0x31,
    0x15,0, 0x26,0xff,0, 0x75,8, 0x95,2, 0x81,2,
    0x85,3, 0x05,0x0c, 0x19,0, 0x2a,0xff,3,
    0x15,0, 0x26,0xff,3, 0x75,16, 0x95,1, 0x81,0, 0xc0,
};

static void init(uni_hid_device_t* d, unsigned buttons) {
    memset(d, 0, sizeof(*d));
    memcpy(d->hid_descriptor, descriptor, sizeof(descriptor));
    d->hid_descriptor[13] = buttons;
    d->hid_descriptor[21] = buttons;
    d->hid_descriptor_len = sizeof(descriptor);
#ifdef TEST_OLD_PARSER
    d->report_parser.init_report = uni_hid_parser_android_init_report;
    d->report_parser.parse_usage = uni_hid_parser_android_parse_usage;
#else
    assert(uni_hid_parser_standard_configure(d));
    d->report_parser.parse_input_report = uni_hid_parser_standard_parse;
#endif
}

int main(void) {
    uni_hid_device_t d;
    init(&d, 8);
    const uint8_t down[] = {1, 1};
    const uint8_t right[] = {2, 255, 128};
    const uint8_t up[] = {1, 0};
    uni_hid_parse_input_report(&d, down, sizeof(down));
    assert(d.controller.gamepad.buttons & BUTTON_A);
    uni_hid_parse_input_report(&d, right, sizeof(right));
    assert(d.controller.gamepad.buttons & BUTTON_A); // Reproduces the old reset bug.
    assert(d.controller.gamepad.axis_x > 400);
    uni_hid_parse_input_report(&d, up, sizeof(up));
    assert(!d.controller.gamepad.buttons && d.controller.gamepad.axis_x > 400);
    const uint8_t select_start[] = {1, 0xc0};
    uni_hid_parse_input_report(&d, select_start, sizeof(select_start));
    assert(d.controller.gamepad.misc_buttons == (MISC_BUTTON_SELECT | MISC_BUTTON_START));
    const uint8_t media[] = {3, 0x24, 2}; // AC Back
    uni_hid_parse_input_report(&d, media, sizeof(media));
    uni_hid_parse_input_report(&d, up, sizeof(up));
    assert(d.controller.gamepad.misc_buttons == MISC_BUTTON_SELECT);
    const uint8_t media_up[] = {3, 0, 0};
    uni_hid_parse_input_report(&d, media_up, sizeof(media_up));
    assert(!d.controller.gamepad.misc_buttons);
    const uint8_t center[] = {2, 128, 128};
    uni_hid_parse_input_report(&d, center, sizeof(center));
    assert(!d.controller.gamepad.axis_x && !d.controller.gamepad.axis_y);
    uni_hid_parse_input_report(&d, down, sizeof(down));
    const uint8_t short_packet[] = {1};
    uni_hid_parse_input_report(&d, short_packet, sizeof(short_packet));
    assert(d.controller.gamepad.buttons & BUTTON_A);
    const uint8_t unknown[] = {99, 0};
    uni_hid_parse_input_report(&d, unknown, sizeof(unknown));
    assert(d.controller.gamepad.buttons & BUTTON_A);
    init(&d, 12);
    const uint8_t compact[] = {1, 4, 3}; // X + Select/Start
    uni_hid_parse_input_report(&d, compact, sizeof(compact));
    assert(d.controller.gamepad.buttons == BUTTON_X);
    assert(d.controller.gamepad.misc_buttons == (MISC_BUTTON_SELECT | MISC_BUTTON_START));
    init(&d, 16);
    const uint8_t android[] = {1, 8, 12};
    uni_hid_parse_input_report(&d, android, sizeof(android));
    assert(d.controller.gamepad.buttons == BUTTON_X);
    assert(d.controller.gamepad.misc_buttons == (MISC_BUTTON_SELECT | MISC_BUTTON_START));
    // A no-ID signed stick + hat, including neutral/null hat and recenter.
    const uint8_t hat_map[] = {
        0x05,1,0x09,5,0xa1,1,0x09,0x30,0x09,0x31,
        0x15,0x80,0x25,0x7f,0x75,8,0x95,2,0x81,2,
        0x09,0x39,0x15,1,0x25,8,0x75,8,0x95,1,0x81,0x42,0xc0};
    memset(&d, 0, sizeof(d));
    memcpy(d.hid_descriptor, hat_map, sizeof(hat_map));
    d.hid_descriptor_len = sizeof(hat_map);
    assert(uni_hid_parser_standard_configure(&d));
    d.report_parser.parse_input_report = uni_hid_parser_standard_parse;
    const uint8_t diagonal[] = {0x80,0x7f,2};
    uni_hid_parse_input_report(&d, diagonal, sizeof(diagonal));
    assert(d.controller.gamepad.axis_x == -512 && d.controller.gamepad.axis_y > 400);
    assert(d.controller.gamepad.dpad == (DPAD_UP | DPAD_RIGHT));
    const uint8_t neutral[] = {0,0,0};
    uni_hid_parse_input_report(&d, neutral, sizeof(neutral));
    assert(!d.controller.gamepad.axis_x && !d.controller.gamepad.axis_y && !d.controller.gamepad.dpad);
    // Button arrays replace only their own report's state; zero releases all.
    const uint8_t array_map[] = {0x05,1,0x09,5,0xa1,1,0x05,9,
        0x19,0,0x29,12,0x15,0,0x25,12,0x75,8,0x95,2,0x81,0,0xc0};
    memset(&d, 0, sizeof(d));
    memcpy(d.hid_descriptor, array_map, sizeof(array_map));
    d.hid_descriptor_len = sizeof(array_map);
    assert(uni_hid_parser_standard_configure(&d));
    d.report_parser.parse_input_report = uni_hid_parser_standard_parse;
    const uint8_t array_down[] = {9,10}, array_up[] = {0,0};
    uni_hid_parse_input_report(&d, array_down, sizeof(array_down));
    assert(d.controller.gamepad.misc_buttons == (MISC_BUTTON_SELECT | MISC_BUTTON_START));
    uni_hid_parse_input_report(&d, array_up, sizeof(array_up));
    assert(!d.controller.gamepad.misc_buttons);
    // Unsupported descriptor features fail configuration explicitly.
    d.hid_descriptor[0] = 0xa4; // Global Push is not implemented by BTstack.
    assert(!uni_hid_parser_standard_configure(&d));
    return 0;
}
