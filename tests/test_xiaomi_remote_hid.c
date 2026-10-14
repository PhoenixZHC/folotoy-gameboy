#include <assert.h>
#include <string.h>
#include "gb_input.h"
#include "uni_hid_device.h"
#include "parser/uni_hid_parser_keyboard.h"

// 无线/输出依赖隔离；报告解码使用生产的 Bluepad32 和 BTstack。
bool uni_hid_device_set_ready_complete(uni_hid_device_t *d) { (void)d; return true; }
gap_connection_type_t gap_get_connection_type(hci_con_handle_t h) { (void)h; return GAP_CONNECTION_INVALID; }
uint8_t hids_client_send_write_report(uint16_t cid, uint8_t id, hid_report_type_t type,
                                    const uint8_t *data, uint8_t len) {
    (void)cid; (void)id; (void)type; (void)data; (void)len;
    return 0;
}

// 按公开协议构造的测试描述符：ID 1，三个 16-bit usage；并非实物抓包。
static const uint8_t array_map[] = {
    0x05,0x01, 0x09,0x06, 0xa1,0x01, 0x85,0x01,
    0x05,0x07, 0x19,0x00, 0x2a,0xff,0x01, 0x15,0x00, 0x26,0xff,0x01,
    0x75,0x10, 0x95,0x03, 0x81,0x00, 0xc0,
    0x05,0x0c, 0x09,0x01, 0xa1,0x01, 0x85,0x02,
    0x15,0x00, 0x26,0xff,0x03, 0x19,0x00, 0x2a,0xff,0x03,
    0x75,0x10, 0x95,0x01, 0x81,0x00, 0xc0,
};
static const uint8_t boot_map[] = {
    0x05,0x01, 0x09,0x06, 0xa1,0x01,
    0x05,0x07, 0x19,0xe0, 0x29,0xe7, 0x15,0x00, 0x25,0x01,
    0x75,0x01, 0x95,0x08, 0x81,0x02,
    0x75,0x08, 0x95,0x01, 0x81,0x01,
    0x19,0x00, 0x2a,0xff,0x00, 0x15,0x00, 0x26,0xff,0x00,
    0x75,0x08, 0x95,0x06, 0x81,0x00, 0xc0,
};
// COM14 实物抓包：RC001、固件 2671，声明 3*16 bit，却发送标准 8 字节键盘负载。
static const uint8_t rc001_map[] = {
    0x05,0x01,0x09,0x06,0xa1,0x01,0x05,0x07,0x09,0x06,0xa1,0x01,
    0x85,0x01,0x95,0x03,0x75,0x10,0x15,0x00,0x25,0xfe,0x19,0x00,
    0x29,0xfe,0x81,0x00,0xc0,0x06,0x00,0xff,0x09,0x00,0xa1,0x01,
    0x85,0x06,0x75,0x08,0x95,0x78,0x15,0x00,0x25,0xff,0x19,0x00,
    0x29,0xff,0x81,0x00,0x85,0x07,0x75,0x08,0x95,0x78,0x15,0x00,
    0x25,0xff,0x19,0x00,0x29,0xff,0x81,0x00,0x85,0x08,0x75,0x08,
    0x95,0x78,0x15,0x00,0x25,0xff,0x19,0x00,0x29,0xff,0x81,0x00,
    0xc0,0xc0,
};
static void setup(uni_hid_device_t *d, bool remote, const uint8_t *map, size_t len) {
    memset(d, 0, sizeof(*d));
    d->vendor_id = remote ? 0x2717 : 0x1234;
    d->product_id = 0x32b8;
    memcpy(d->hid_descriptor, map, len);
    d->hid_descriptor_len = len;
    d->report_parser.init_report = uni_hid_parser_keyboard_init_report;
    d->report_parser.parse_input_report = uni_hid_parser_keyboard_parse_input_report;
    d->report_parser.parse_usage = uni_hid_parser_keyboard_parse_usage;
    uni_hid_parser_keyboard_setup(d);
}
static pad_sample_t input(uni_hid_device_t *d, const uint8_t *data, size_t len) {
    uni_hid_parse_input_report(d, data, len);
    assert(d->controller.klass == UNI_CONTROLLER_CLASS_KEYBOARD);
    return gb_input_keyboard(d->controller.keyboard.pressed_keys,
                             UNI_KEYBOARD_PRESSED_KEYS_MAX, d->controller.keyboard.modifiers);
}
static pad_sample_t keys(uni_hid_device_t *d, uint16_t a, uint16_t b, uint16_t c) {
    const uint8_t data[] = {1, a & 255, a >> 8, b & 255, b >> 8, c & 255, c >> 8};
    return input(d, data, sizeof(data));
}
static unsigned mask(pad_sample_t p) {
    return p.up | (p.left << 1) | (p.down << 2) | (p.right << 3) |
           (p.a << 4) | (p.b << 5) | (p.menu << 6) | (p.view << 7);
}
int main(void) {
    uni_hid_device_t d;
    setup(&d, true, array_map, sizeof(array_map));
    assert(uni_hid_parser_keyboard_is_xiaomi_remote(&d));
    assert(!uni_hid_parser_keyboard_is_xiaomi_remote(NULL));
    const uint16_t physical[] = {0x4f,0x52,0x50,0x51,0x35,0x65,0x4a,0xf1};
    for (unsigned i = 0; i < 8; i++) {
        pad_sample_t p = keys(&d, physical[i], 0, 0);
        assert(p.connected && p.any_button && mask(p) == (1u << i));
        assert(mask(keys(&d, physical[i], 0, 0)) == (1u << i)); // 长按重复不丢键。
        p = keys(&d, 0, 0, 0);
        assert(!p.any_button && !mask(p));
    }
    assert(mask(keys(&d, 0xc0,0x5d,0x24)) == 0x70); // 公开的键码别名。
    pad_sample_t p = keys(&d, 0x4f,0x35,0x65);
    assert(mask(p) == 0x31); // 方向 + A + B 按报告中完整集合保留。
    const uint8_t media[] = {2,0xe9,0}, media_up[] = {2,0,0};
    assert(mask(input(&d, media, sizeof(media))) == 0x31);
    assert(mask(input(&d, media_up, sizeof(media_up))) == 0x31);
    assert(mask(keys(&d, 0x4f,0,0)) == 1); // 松开攻击键，方向仍按住。
    assert(mask(keys(&d, 0x51,0,0)) == 8); // 新报告替换旧方向，不能累积卡键。
    assert(!mask(keys(&d, 0x135,0,0))); // 16-bit usage 不可截断成 TV。
    assert(!mask(keys(&d, 1,0x35,0x4f))); // ErrorRollOver 释放整组游戏输入。
    const uint16_t unused[] = {0x28,0x66,0x3e,0x80,0x81};
    for (unsigned i=0; i<sizeof(unused)/sizeof(unused[0]); i++) {
        p = keys(&d, unused[i],0,0);
        assert(!p.any_button && !mask(p));
    }
    keys(&d, 0x4f,0x35,0);
    const uint8_t short_report[] = {1,0x4f,0};
    p = input(&d, short_report, sizeof(short_report));
    assert(!p.any_button && !mask(p));
    const uint8_t unknown_id[] = {3,0x4f,0,0,0,0,0};
    assert(!mask(input(&d, unknown_id, sizeof(unknown_id))));

    // 返回键复用真实 Select 长按状态机，并需释放后重新触发。
    gb_select_pause_t pause;
    gb_select_pause_init(&pause, keys(&d,0,0,0));
    p = keys(&d,0xf1,0,0);
    assert(!gb_select_pause_step(&pause,p,100));
    assert(!gb_select_pause_step(&pause,p,1000099));
    assert(gb_select_pause_step(&pause,p,1000100));
    assert(!gb_select_pause_step(&pause,p,2000100));
    assert(!gb_select_pause_step(&pause,keys(&d,0,0,0),2000200));

    // 无 Report ID 的标准键盘布局由实际描述符决定，不猜测包长。
    setup(&d,true,boot_map,sizeof(boot_map));
    const uint8_t boot[] = {0,0,0x50,0x35,0x65,0xf1,0,0};
    p = input(&d,boot,sizeof(boot));
    assert(mask(p) == 0xb4);
    const uint8_t released[8] = {0};
    assert(!mask(input(&d,released,sizeof(released))));

    // 相同 usage 在其他 VID 的键盘上不旋转、不变成游戏 A/B。
    setup(&d,false,array_map,sizeof(array_map));
    assert(!uni_hid_parser_keyboard_is_xiaomi_remote(&d));
    assert(!mask(keys(&d,0x4f,0x35,0x65)));
    assert(mask(keys(&d,0x07,0x0d,0x0e)) == 0x38); // 普通 D/J/K 原映射不变。
    d.vendor_id = 0x2717; d.product_id = 0x1234;
    assert(!uni_hid_parser_keyboard_is_xiaomi_remote(&d));
    // 重放本次实物的包格式和八键键码，含 BTstack 加入的 Report ID。
    setup(&d,true,rc001_map,sizeof(rc001_map));
    for (unsigned i=0; i<8; i++) {
        const uint8_t captured[] = {1,0,0,physical[i],0,0,0,0,0};
        p = input(&d,captured,sizeof(captured));
        assert(mask(p)==(1u<<i));
        const uint8_t captured_up[] = {1,0,0,0,0,0,0,0,0};
        assert(!mask(input(&d,captured_up,sizeof(captured_up))));
    }
    // 组合包是构造回归数据，不能证明实物支持多键。
    const uint8_t combination[] = {1,0,0,0x4f,0x35,0x65,0,0,0};
    assert(mask(input(&d,combination,sizeof(combination)))==0x31);
    const uint8_t only_direction[] = {1,0,0,0x4f,0,0,0,0,0};
    assert(mask(input(&d,only_direction,sizeof(only_direction)))==1);
    const uint8_t bad_reserved[] = {1,0,1,0x4f,0,0,0,0,0};
    assert(!mask(input(&d,bad_reserved,sizeof(bad_reserved))));
    const uint8_t overflow_rc001[] = {1,0,0,1,0x35,0,0,0,0};
    assert(!mask(input(&d,overflow_rc001,sizeof(overflow_rc001))));
    assert(!mask(input(&d,combination,sizeof(combination)-1))); // 截断仍拒绝。
    return 0;
}
