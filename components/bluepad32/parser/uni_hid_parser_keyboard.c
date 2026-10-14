// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Ricardo Quesada
// http://retro.moe/unijoysticle2

#include "parser/uni_hid_parser_keyboard.h"

#include <math.h>
#include <time.h>

#include "controller/uni_controller.h"
#include "hid_usage.h"
#include "uni_common.h"
#include "uni_hid_device.h"
#include "uni_log.h"

// JX_05 Keyboard: https://www.aliexpress.us/item/3256805596713243.html
#define JX_05_VID 0x05ac
#define JX_05_PID 0x022c

// Valid for JX-05 "keyboard"
typedef struct {
    int32_t x;
    int32_t y;
    bool ready_to_process;
    bool last_report;
    bool is_down_or_button;
} keyboard_jx_05_t;

typedef struct {
    int pressed_key_index;
    bool keyboard_page_seen;
    bool using_xiaomi;
    bool ignore_report;
    bool xiaomi_boot_layout_logged;

    // TODO: JX_05 parser should be moved to its own parser... when the Keyboard parser becomes unmaintainable.
    bool using_jx_05;
    keyboard_jx_05_t jx_05;
} keyboard_instance_t;

static keyboard_instance_t* get_keyboard_instance(uni_hid_device_t* d);

bool uni_hid_parser_keyboard_is_xiaomi_remote(const uni_hid_device_t* d) {
    return d && d->vendor_id == 0x2717 && d->product_id == 0x32b8;
}

// 小米遥控器横握：将原始 Keyboard/Keypad usage 转成应用现有的 WASD/J/K/I/L。
// 只在已识别遥控器上执行，普通键盘和其他手柄不经过此映射。
static uint16_t xiaomi_horizontal_key(uint16_t usage) {
    switch (usage) {
        case 0x4f: return 0x1a; // 右 -> W（上）
        case 0x52: return 0x04; // 上 -> A（左）
        case 0x50: return 0x16; // 左 -> S（下）
        case 0x51: return 0x07; // 下 -> D（右）
        case 0x35: case 0xc0: return 0x0d; // TV -> J（游戏 A）
        case 0x65: case 0x5d: return 0x0e; // 三横线菜单 -> K（游戏 B）
        case 0x4a: case 0x24: return 0x0f; // 主页 -> L（Start）
        case 0xf1: return 0x0c; // 返回 -> I（Select）；不能先按标准键盘过滤它。
        case 1: case 2: case 3: return usage; // 错误报告交由输入层松开全部游戏键。
        default: return 0; // 确认、电源、语音、音量没有分配游戏动作。
    }
}

static void jx_05_parse_usage(uni_hid_device_t* d,
                              const hid_globals_t* globals,
                              uint16_t usage_page,
                              uint16_t usage,
                              int32_t value) {
    keyboard_instance_t* ins = get_keyboard_instance(d);
    int idx = ins->pressed_key_index;

    switch (usage_page) {
        case HID_USAGE_PAGE_GENERIC_DESKTOP:
            switch (usage) {
                case HID_USAGE_AXIS_X:
                    ins->jx_05.x = uni_hid_parser_process_axis(globals, value);
                    break;
                case HID_USAGE_AXIS_Y:
                    ins->jx_05.y = uni_hid_parser_process_axis(globals, value);
                    break;
                default:
                    // Only report unsupported values if they are not 0
                    if (value)
                        logi("Keyboard: Unsupported page: 0x%04x, usage: 0x%04x, value=0x%x\n", usage_page, usage,
                             value);
                    break;
            }
            break;
        case HID_USAGE_PAGE_DIGITIZER:
            switch (usage) {
                case HID_USAGE_IN_RANGE:
                    // Used by JX05, ignore it
                    break;
                case HID_USAGE_TIP_SWITCH:
                    // The last usage of the last report sets the "tip switch" as false.
                    // To avoid processing the last report, we add the extra variable "last_report"
                    if (value && ins->jx_05.last_report)
                        ins->jx_05.ready_to_process = true;

                    // Must be checked after previous condition
                    ins->jx_05.last_report = !value;
                    break;
                case HID_USAGE_CONTACT_IDENTIFIER:
                    // Used by JX05, ignore it
                    break;
                case HID_USAGE_CONTACT_COUNT: {
                    int32_t x = ins->jx_05.x;
                    int32_t y = ins->jx_05.y;

                    if (ins->jx_05.is_down_or_button) {
                        // The previous report was "down" or "button". This report should determine which one is one.
                        ins->jx_05.is_down_or_button = false;

                        // Button repeats the first and last (second) report coordinates
                        if (x == -260 && y == 145)
                            d->controller.keyboard.pressed_keys[idx++] = HID_USAGE_KB_SPACEBAR;
                        else
                            d->controller.keyboard.pressed_keys[idx++] = HID_USAGE_KB_DOWN_ARROW;
                    }
                    if (ins->jx_05.ready_to_process) {
                        // This is the last usage in the JX05 report.
                        // JX05 simulates mouse movements by injecting different reports for a single movement.
                        // We only care about the first report of each "movement".
                        //  x=-260, y=-222 / ... / x=-260, y=488, and tip_switch=false, "scroll up"
                        //  x=-260, y=145  / ... / x=-260, y=-454, and tip_switch=false, "scroll down"
                        //  x=-387, y=251  / ... / x= 225, y=251, and tip_switch=false, "scroll left"
                        //  x=-48,  y=251  / ... / x=-467, y=251, and tip_switch=false, "scroll right"
                        //  x=-260, y=145  / x=-260, y=145, and tip_switch=false, "button"
                        if (x == -260 && y == -222)
                            d->controller.keyboard.pressed_keys[idx++] = HID_USAGE_KB_UP_ARROW;
                        else if (x == -260 && y == 145)
                            // Could either be "down" or "press". The next packet decides
                            ins->jx_05.is_down_or_button = true;
                        else if (x == -387 && y == 251)
                            d->controller.keyboard.pressed_keys[idx++] = HID_USAGE_KB_LEFT_ARROW;
                        else if (x == -48 && y == 251)
                            d->controller.keyboard.pressed_keys[idx++] = HID_USAGE_KB_RIGHT_ARROW;
                        else
                            break;
                        ins->pressed_key_index = idx;
                        ins->jx_05.ready_to_process = false;
                    }
                    break;
                }
                default:
                    break;
            }
            break;
        default:
            break;
    }
}

void uni_hid_parser_keyboard_setup(uni_hid_device_t* d) {
    uni_hid_device_set_ready_complete(d);

    keyboard_instance_t* ins = get_keyboard_instance(d);
    memset(ins, 0, sizeof(*ins));
    ins->using_jx_05 = (d->vendor_id == JX_05_VID && d->product_id == JX_05_PID);
    ins->using_xiaomi = uni_hid_parser_keyboard_is_xiaomi_remote(d);
    if (ins->using_jx_05) {
        ins->jx_05.ready_to_process = true;
    }
}

void uni_hid_parser_keyboard_parse_input_report(struct uni_hid_device_s* d, const uint8_t* report, uint16_t len) {
    keyboard_instance_t* ins = get_keyboard_instance(d);
    if (!ins->using_xiaomi) return;
    // 依照设备实际 Report Map 校验长度，避免截断报告保留半个组合键。
    bool has_id = btstack_hid_report_id_declared(d->hid_descriptor, d->hid_descriptor_len);
    uint16_t id = has_id && report && len ? report[0] : HID_REPORT_ID_UNDEFINED;
    int expected = btstack_hid_get_report_size_for_id(id, HID_REPORT_TYPE_INPUT,
                                                     d->hid_descriptor, d->hid_descriptor_len);
    // RC001 FW2671 实测：Report Map 声明三个 16-bit usage，通知却是
    // modifiers/reserved/六个 8-bit usage。BTstack 加入 ID 后总长为 9。
    // 仅在小米身份 + 报告 1 + 此长度/保留字节特征下处理，不放宽通用校验。
    if (report && has_id && id == 1 && expected == 6 && len == 9 && report[2] == 0) {
        memset(&d->controller.keyboard, 0, sizeof(d->controller.keyboard));
        ins->ignore_report = true; // 已直接解码，禁止描述符解析器再次按 16-bit 解释。
        if (!ins->xiaomi_boot_layout_logged) {
            logi("MI RC: HID map says 6 bytes; decoding observed 8-byte keyboard payload\n");
            ins->xiaomi_boot_layout_logged = true;
        }
        int index = 0;
        for (unsigned i = 3; i < len; ++i) {
            uint16_t key = xiaomi_horizontal_key(report[i]);
            if (key >= 1 && key <= 3) {
                memset(&d->controller.keyboard, 0, sizeof(d->controller.keyboard));
                d->controller.keyboard.pressed_keys[0] = key;
                return;
            }
            if (key) d->controller.keyboard.pressed_keys[index++] = key;
        }
        return;
    }
    if (!report || !expected || len != expected + (has_id ? 1 : 0)) {
        memset(&d->controller.keyboard, 0, sizeof(d->controller.keyboard));
        ins->ignore_report = true;
        loge("MI RC: invalid HID input report id=%u len=%u expected=%d\n", id, len, expected);
    }
}

void uni_hid_parser_keyboard_init_report(uni_hid_device_t* d) {
    // Reset pressed key index
    keyboard_instance_t* ins = get_keyboard_instance(d);
    ins->pressed_key_index = 0;
    ins->keyboard_page_seen = false;
    ins->ignore_report = false;

    // Ordinary keyboards may use separate report IDs for media keys. Keep
    // their held keyboard keys until a Keyboard/Keypad report arrives.
    uni_controller_t* ctl = &d->controller;
    if (ins->using_jx_05) memset(ctl, 0, sizeof(*ctl));
    ctl->klass = UNI_CONTROLLER_CLASS_KEYBOARD;
}

void uni_hid_parser_keyboard_parse_usage(uni_hid_device_t* d,
                                         const hid_globals_t* globals,
                                         uint16_t usage_page,
                                         uint16_t usage,
                                         int32_t value) {
    keyboard_instance_t* ins = get_keyboard_instance(d);
    if (ins->ignore_report) return;
    if (ins->using_jx_05) {
        jx_05_parse_usage(d, globals, usage_page, usage, value);
        // fall-though, don't return
    }

    logd("usage page=%#x, usage=%#x, value=%d\n", usage_page, usage, value);

    int idx = ins->pressed_key_index;
    switch (usage_page) {
        case HID_USAGE_PAGE_KEYBOARD_KEYPAD:
            if (!ins->keyboard_page_seen) {
                memset(&d->controller.keyboard, 0, sizeof(d->controller.keyboard));
                ins->keyboard_page_seen = true;
            }
            if (ins->using_xiaomi) usage = xiaomi_horizontal_key(usage);
            if (value) {
                if (ins->using_xiaomi && !usage) break;
                if (usage < HID_USAGE_KB_LEFT_CONTROL) {
                    if (idx >= UNI_KEYBOARD_PRESSED_KEYS_MAX) {
                        loge("Keyboard: Reached max keyboard keys, skipping page: %d, usage: %d, value: %d\n",
                             usage_page, usage, value);
                        return;
                    }
                    // "usage" represents the pressed key.
                    // See: USB HID Usage Tables, Section 10 (page 53).
                    d->controller.keyboard.pressed_keys[idx++] = usage;
                    ins->pressed_key_index = idx;
                } else if (usage <= HID_USAGE_KB_RIGHT_GUI) {
                    // Value is between 0xe0 and 0xe7: the modifiers
                    // Modifier is between 0 - 7
                    uint8_t modifier = usage - HID_USAGE_KB_LEFT_CONTROL;
                    d->controller.keyboard.modifiers |= BIT(modifier);
                } else {
                    // Usage >= 0xe8, unsupported value.
                    logi("Keyboard: unsupported page:%d, usage:%d, value:%d\n", usage_page, usage, value);
                }
            }
            break;

        case HID_USAGE_PAGE_CONSUMER:
            // Media keys are not Game Boy controls. Do not overwrite the
            // held letter keys when a separate consumer report arrives.
            if (!ins->using_jx_05) break;
            if (!value)
                break;
            // To support TikTok Ring Controller and "5-button keyboard". See:
            // https://github.com/ricardoquesada/bluepad32/issues/68
            // https://github.com/ricardoquesada/bluepad32/issues/104
            switch (usage) {
                case 0:
                    // Undefined, used by 8BitDo Retro Keyboard
                    break;
                // Used by "TikTog Ring Controller"
                case HID_USAGE_POWER:
                    d->controller.keyboard.pressed_keys[idx++] = HID_USAGE_KB_POWER;
                    ins->pressed_key_index = idx;
                    break;
                case HID_USAGE_VOLUME_UP:
                    d->controller.keyboard.pressed_keys[idx++] = HID_USAGE_KB_VOLUME_UP;
                    ins->pressed_key_index = idx;
                    break;
                case HID_USAGE_VOLUME_DOWN:
                    d->controller.keyboard.pressed_keys[idx++] = HID_USAGE_KB_VOLUME_DOWN;
                    ins->pressed_key_index = idx;
                    break;
                case HID_USAGE_AC_HOME:
                    d->controller.keyboard.pressed_keys[idx++] = HID_USAGE_KB_HOME;
                    ins->pressed_key_index = idx;
                    break;
                case HID_USAGE_AC_SCROLL_UP:
                    d->controller.keyboard.pressed_keys[idx++] = HID_USAGE_KB_PAGE_UP;
                    ins->pressed_key_index = idx;
                    break;
                case HID_USAGE_AC_SCROLL_DOWN:
                    d->controller.keyboard.pressed_keys[idx++] = HID_USAGE_KB_PAGE_DOWN;
                    ins->pressed_key_index = idx;
                    break;
                // Used by "5-button keyboard"
                case HID_USAGE_SCAN_NEXT_TRACK:
                    d->controller.keyboard.pressed_keys[idx++] = HID_USAGE_KB_RIGHT_ARROW;
                    ins->pressed_key_index = idx;
                    break;
                case HID_USAGE_SCAN_PREVIOUS_TRACK:
                    d->controller.keyboard.pressed_keys[idx++] = HID_USAGE_KB_LEFT_ARROW;
                    ins->pressed_key_index = idx;
                    break;
                case HID_USAGE_PLAY_PAUSE:
                    d->controller.keyboard.pressed_keys[idx++] = HID_USAGE_KB_PAUSE;
                    ins->pressed_key_index = idx;
                    break;
                default:
                    logi("Keyboard: Unsupported page: 0x%04x, usage: 0x%04x, value=0x%x\n", usage_page, usage, value);
                    break;
            }
            break;

            // unknown usage page
        default:
            logd("Keyboard: Unsupported page: 0x%04x, usage: 0x%04x, value=0x%x\n", usage_page, usage, value);
            break;
    }
}

void uni_hid_parser_keyboard_device_dump(struct uni_hid_device_s* d) {
    ARG_UNUSED(d);

    logi("\tuni_hid_parser_keyboard_device_dump: implement me: \n");
}

void uni_hid_parser_keyboard_set_leds(struct uni_hid_device_s* d, uint8_t led_bitmask) {
    uint8_t status;
    gap_connection_type_t type;

    if (!d) {
        loge("Keyboard: Invalid device\n");
        return;
    }

    type = gap_get_connection_type(d->conn.handle);

    if (type == GAP_CONNECTION_LE) {
        // TODO: Which is the report Id ? Is it always 1 ?
        status = hids_client_send_write_report(d->hids_cid, 1, HID_REPORT_TYPE_OUTPUT, &led_bitmask, 1);
        // TODO: If error, it should retry
        if (status != ERROR_CODE_SUCCESS)
            logi("Keyboard: Failed to send LED report, error=%#x\n", status);
    } else {
        logi("Keyboard: Set LED report not implemented for BR/EDR yet\n");
    }
}

// Helpers
static keyboard_instance_t* get_keyboard_instance(uni_hid_device_t* d) {
    return (keyboard_instance_t*)&d->parser_data[0];
}
