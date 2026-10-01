#include <assert.h>
#include <string.h>
#include "gb_input.h"
#include "uni_hid_device.h"
#include "parser/uni_hid_parser_keyboard.h"

// Only radio/output dependencies are stubbed. Input reports use the actual
// vendored Bluepad32 + BTstack descriptor parsers.
bool uni_hid_device_set_ready_complete(uni_hid_device_t *device) {
    (void)device;
    return true;
}
gap_connection_type_t gap_get_connection_type(hci_con_handle_t handle) {
    (void)handle;
    return GAP_CONNECTION_INVALID;
}
uint8_t hids_client_send_write_report(uint16_t cid, uint8_t id, hid_report_type_t type,
                                     const uint8_t *report, uint8_t len) {
    (void)cid; (void)id; (void)type; (void)report; (void)len;
    return 0;
}

static const uint8_t descriptor[] = {
    0x05,0x01, 0x09,0x06, 0xa1,0x01, 0x85,0x01,
    0x05,0x07, 0x19,0xe0, 0x29,0xe7, 0x15,0x00, 0x25,0x01,
    0x75,0x01, 0x95,0x08, 0x81,0x02,
    0x75,0x08, 0x95,0x01, 0x81,0x01,
    0x19,0x00, 0x29,0x65, 0x15,0x00, 0x25,0x65,
    0x75,0x08, 0x95,0x06, 0x81,0x00, 0xc0,
    0x05,0x0c, 0x09,0x01, 0xa1,0x01, 0x85,0x02,
    0x15,0x00, 0x26,0xff,0x03, 0x19,0x00, 0x2a,0xff,0x03,
    0x75,0x10, 0x95,0x01, 0x81,0x00, 0xc0,
};

static pad_sample_t report(uni_hid_device_t *d, const uint8_t *bytes, size_t len) {
    uni_hid_parse_input_report(d, bytes, (uint16_t)len);
    assert(d->controller.klass == UNI_CONTROLLER_CLASS_KEYBOARD);
    return gb_input_keyboard(d->controller.keyboard.pressed_keys,
                             UNI_KEYBOARD_PRESSED_KEYS_MAX, d->controller.keyboard.modifiers);
}

int main(void) {
    uni_hid_device_t d = {0};
    memcpy(d.hid_descriptor, descriptor, sizeof(descriptor));
    d.hid_descriptor_len = sizeof(descriptor);
    d.report_parser.init_report = uni_hid_parser_keyboard_init_report;
    d.report_parser.parse_input_report = uni_hid_parser_keyboard_parse_input_report;
    d.report_parser.parse_usage = uni_hid_parser_keyboard_parse_usage;
    uni_hid_parser_keyboard_setup(&d);
    const uint8_t down[] = {1, 2, 0, 0x07, 0x0d, 0x0e, 0, 0, 0};
    pad_sample_t p = report(&d, down, sizeof(down));
    assert(p.right && p.a && p.b && p.any_button);
    const uint8_t volume[] = {2, 0xe9, 0};
    p = report(&d, volume, sizeof(volume));
    assert(p.right && p.a && p.b); // Media report must not release held letters.
    const uint8_t media_release[] = {2, 0, 0};
    p = report(&d, media_release, sizeof(media_release));
    assert(p.right && p.a && p.b);
    const uint8_t up[] = {1, 0, 0, 0, 0, 0, 0, 0, 0};
    p = report(&d, up, sizeof(up));
    assert(!p.right && !p.a && !p.b && !p.any_button);
    const uint8_t overflow[] = {1, 0, 0, 1, 1, 1, 1, 1, 1};
    p = report(&d, overflow, sizeof(overflow));
    assert(!p.any_button && !p.a && !p.right);
    // The same standard array report without Report IDs (boot-style layout).
    memcpy(d.hid_descriptor, descriptor, 6);
    memcpy(d.hid_descriptor + 6, descriptor + 8, 37);
    d.hid_descriptor_len = 43;
    p = report(&d, down + 1, sizeof(down) - 1);
    assert(p.right && p.a && p.b);
    p = report(&d, up + 1, sizeof(up) - 1);
    assert(!p.any_button && !p.right);
    // NKRO-style one-bit-per-key report, not a fixed six-key array.
    const uint8_t bitmap_descriptor[] = {
        0x05,0x01, 0x09,0x06, 0xa1,0x01, 0x05,0x07,
        0x19,0x00, 0x29,0x1f, 0x15,0x00, 0x25,0x01,
        0x75,0x01, 0x95,0x20, 0x81,0x02, 0xc0,
    };
    memcpy(d.hid_descriptor, bitmap_descriptor, sizeof(bitmap_descriptor));
    d.hid_descriptor_len = sizeof(bitmap_descriptor);
    const uint8_t bitmap[] = {0x80, 0x60, 0, 0x04}; // D, J, K, W
    p = report(&d, bitmap, sizeof(bitmap));
    assert(p.right && p.up && p.a && p.b);
    const uint8_t bitmap_up[] = {0, 0, 0, 0};
    p = report(&d, bitmap_up, sizeof(bitmap_up));
    assert(!p.any_button && !p.right && !p.up && !p.a);
    return 0;
}
