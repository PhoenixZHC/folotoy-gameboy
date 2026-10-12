// SPDX-License-Identifier: MIT
// Copyright (c) 2026 1ikeMcFlurry
// L1241 FW 27.02 transport adapted from esp32c3-iine-gamepad.
// See third_party_licenses/iine-gamepad-MIT.txt.
// Pair with Pairing + A held for about 3 seconds, then select IINE GAMEPAD.
#include "bt/uni_bt_le_iine.h"

#include <string.h>
#include "parser/uni_iine_protocol.h"
#include "uni_config.h"
#include "uni_log.h"

// Configuration service only. Never open the separate OTA service.
#define IINE_UUID(id) {0xd7,0xf0,0x10,id,0x66,0x0d,0x46,0xe9,0x96,0xc3,0x19,0xc4,0x14,0x8b,0xda,0xb5}
static const uint8_t service_uuid[] = IINE_UUID(0xe0);
static const uint8_t write_uuid[] = IINE_UUID(0xe1);
static const uint8_t notify_uuid[] = IINE_UUID(0xe2);
static const uint8_t mode_payload[] = {2, 0xdf, 0xab};
static const uint8_t test_payload[] = {5, 0xdf, 0xab, 0, 6, 5};
static const uint8_t caps_payload[] = {0, 1};
enum { REQUEST_TIMEOUT_MS = 4500, TICK_MS = 50 };
typedef enum { SERVICE, CHARACTERISTICS, SUBSCRIBE, INFO, CONFIRM, CAPS, MODE, DELAY, TEST, RUNNING } step_t;
typedef struct {
    uni_hid_device_t* device;
    hci_con_handle_t handle;
    step_t step;
    bool busy, listening, replied, ready;
    uint8_t serial_counter, serial, attempts;
    uint16_t firmware;
    uint32_t deadline, next_keepalive;
    gatt_client_service_t service;
    gatt_client_characteristic_t write, notify;
    gatt_client_notification_t listener;
    btstack_timer_source_t timer;
    uint8_t wire[IINE_PACKET_MAX]; // GATT borrows this until write completion.
    iine_packet_t reply;
    iine_input_state_t input; // Canonical delta state, never remapped in place.
} iine_link_t;
static iine_link_t links[CONFIG_BLUEPAD32_MAX_DEVICES];
static uni_iine_confirmation_cb_t confirmation_callback;
static void packet_handler(uint8_t type, uint16_t channel, uint8_t* packet, uint16_t size);

static bool due(uint32_t now, uint32_t deadline) { return (int32_t)(now - deadline) >= 0; }
static iine_link_t* for_handle(hci_con_handle_t handle) {
    for (size_t i = 0; i < ARRAY_SIZE(links); ++i)
        if (links[i].device && links[i].handle == handle) return &links[i];
    return NULL;
}

void uni_bt_le_iine_reset(uni_hid_device_t* device) {
    if (!device) return;
    for (size_t i = 0; i < ARRAY_SIZE(links); ++i) {
        iine_link_t* s = &links[i];
        if (s->device != device) continue;
        if (confirmation_callback) confirmation_callback(device, false, 0);
        btstack_run_loop_remove_timer(&s->timer);
        if (s->listening) gatt_client_stop_listening_for_characteristic_value_updates(&s->listener);
        memset(s, 0, sizeof(*s));
    }
}

static void fail(iine_link_t* s, const char* reason) {
    uni_hid_device_t* device = s->device;
    loge("IINE L1241: %s\n", reason);
    uni_bt_le_iine_reset(device);
    // Normal disconnection processing clears platform input and deletes the slot.
    uni_hid_device_disconnect(device);
}

static void advance(iine_link_t* s, step_t step) {
    s->step = step;
    s->attempts = 0;
    s->replied = false;
    s->deadline = btstack_run_loop_get_time_ms();
}

void uni_bt_le_iine_set_confirmation_callback(uni_iine_confirmation_cb_t callback) {
    confirmation_callback = callback;
}

uint16_t uni_bt_le_iine_firmware(const uni_hid_device_t* device) {
    if (!device) return 0;
    iine_link_t* s = for_handle(device->conn.handle);
    return s && s->device == device ? s->firmware : 0;
}

void uni_bt_le_iine_confirm(uni_hid_device_t* device) {
    if (!device) return;
    iine_link_t* s = for_handle(device->conn.handle);
    if (!s || s->device != device || s->step != CONFIRM) return;
    if (due(btstack_run_loop_get_time_ms(), s->deadline)) {
        fail(s, "firmware confirmation timed out");
        return;
    }
    if (confirmation_callback) confirmation_callback(device, false, 0);
    logi("IINE L1241: user accepted unverified firmware for this connection\n");
    btstack_run_loop_remove_timer(&device->connection_timer);
    btstack_run_loop_set_timer(&device->connection_timer, 30000);
    btstack_run_loop_add_timer(&device->connection_timer);
    advance(s, CAPS);
}

static bool check_start(iine_link_t* s, uint8_t status) {
    if (status != ERROR_CODE_SUCCESS) {
        fail(s, "GATT request rejected");
        return false;
    }
    s->busy = true;
    s->deadline = btstack_run_loop_get_time_ms() + REQUEST_TIMEOUT_MS;
    return true;
}

static bool send_request(iine_link_t* s, uint8_t opcode, const uint8_t* payload, size_t length) {
    if (s->busy || !iine_request_allowed(opcode, payload, length)) return false;
    ++s->serial_counter;
    s->serial = (s->serial_counter & 0x7f) | ((s->serial_counter & 1) ? 0 : 0x80);
    s->replied = false;
    // Nonce is protocol scrambling, not a security key. BLE handles encryption.
    size_t bytes = iine_packet_build(s->wire, sizeof(s->wire), opcode, s->serial,
                                    (uint8_t)btstack_run_loop_get_time_ms(), payload, length);
    if (!bytes) { fail(s, "invalid local request"); return false; }
    return check_start(s, gatt_client_write_value_of_characteristic(
                             packet_handler, s->handle, s->write.value_handle, bytes, s->wire));
}

static void publish_input(iine_link_t* s) {
    const iine_input_state_t* in = &s->input;
    uni_controller_t* ctl = &s->device->controller;
    ctl->klass = UNI_CONTROLLER_CLASS_GAMEPAD;
    ctl->gamepad = (uni_gamepad_t){
        .dpad = in->dpad,
        .buttons = (in->buttons & 0xff) | ((in->buttons >> 2) & 0x300),
        .misc_buttons = ((in->buttons & (1u << 8)) ? MISC_BUTTON_SELECT : 0) |
                        ((in->buttons & (1u << 9)) ? MISC_BUTTON_START : 0),
        .axis_x = (in->axes[0] - 128) * 4, .axis_y = (in->axes[1] - 128) * 4,
        .axis_rx = (in->axes[3] - 128) * 4, .axis_ry = (in->axes[4] - 128) * 4,
        .brake = in->axes[2] * 1023 / 255, .throttle = in->axes[5] * 1023 / 255,
    };
    uni_hid_device_process_controller(s->device);
}

static void tick(btstack_timer_source_t* timer) {
    iine_link_t* s = btstack_run_loop_get_timer_context(timer);
    if (!s || !s->device) return;
    uint32_t now = btstack_run_loop_get_time_ms();
    if (s->busy) {
        if (due(now, s->deadline)) fail(s, "GATT timeout");
    } else if (s->step == INFO || s->step == CAPS) {
        if (s->replied) {
            if (s->step == INFO) {
                static const uint8_t identity[] = {7, 0x10, 2, 0x20};
                if (s->reply.length != 8 || memcmp(s->reply.payload, identity, sizeof(identity))) {
                    fail(s, "unsupported identity (requires L1241 VID/PID)");
                } else if (s->reply.payload[4] == 0x27 && s->reply.payload[5] == 2) {
                    s->firmware = 0x2702;
                    logi("IINE L1241 FW 27.02 identified\n");
                    advance(s, CAPS);
                } else if (confirmation_callback) {
                    uint16_t firmware = ((uint16_t)s->reply.payload[4] << 8) | s->reply.payload[5];
                    s->firmware = firmware;
                    advance(s, CONFIRM);
                    s->deadline = now + 60000;
                    btstack_run_loop_remove_timer(&s->device->connection_timer);
                    btstack_run_loop_set_timer(&s->device->connection_timer, 65000);
                    btstack_run_loop_add_timer(&s->device->connection_timer);
                    logi("IINE L1241 FW %02x.%02x is unverified; waiting for user\n",
                         s->reply.payload[4], s->reply.payload[5]);
                    confirmation_callback(s->device, true, firmware);
                } else {
                    fail(s, "unverified firmware requires explicit confirmation");
                }
            } else advance(s, MODE);
        } else if (due(now, s->deadline)) {
            if (s->attempts >= 2) {
                if (s->step == INFO) fail(s, "identity reply timeout");
                else advance(s, MODE); // Capability reply is optional, as in the measured demo.
            } else {
                ++s->attempts;
                send_request(s, s->step == INFO ? 0x91 : 0xb0,
                             s->step == CAPS ? caps_payload : NULL,
                             s->step == CAPS ? sizeof(caps_payload) : 0);
            }
        }
    } else if (s->step == CONFIRM) {
        if (due(now, s->deadline)) fail(s, "firmware confirmation timed out");
    } else if (s->step == MODE) {
        if (send_request(s, 0x1d, mode_payload, sizeof(mode_payload))) {
            s->step = DELAY;
            s->next_keepalive = now + 500;
        }
    } else if (s->step == DELAY && due(now, s->next_keepalive)) {
        if (send_request(s, 0x1d, test_payload, sizeof(test_payload))) s->step = TEST;
    } else if (s->step == TEST) {
        // Only expose a usable controller after test-page write completion.
        s->step = RUNNING;
        s->device->controller.klass = UNI_CONTROLLER_CLASS_GAMEPAD;
        s->device->controller_type = k_eControllerType_GenericController;
        uni_hid_device_connect(s->device);
        if (!uni_hid_device_set_ready_complete(s->device)) return;
        s->ready = true;
        s->next_keepalive = now + 1000;
        publish_input(s);
    } else if (s->step == RUNNING && due(now, s->next_keepalive)) {
        send_request(s, 0x1d, test_payload, sizeof(test_payload));
        s->next_keepalive = now + 1000;
    }
    // Reports are deltas: quiet time must not release a held button.
    if (s->device) {
        btstack_run_loop_set_timer(timer, TICK_MS);
        btstack_run_loop_add_timer(timer);
    }
}

static void packet_handler(uint8_t type, uint16_t channel, uint8_t* packet, uint16_t size) {
    (void)channel;
    if (type != HCI_EVENT_PACKET || size < 4 || (uint16_t)packet[1] + 2 != size) return;
    // These GATT events all put the connection handle at offset 2.
    iine_link_t* s = for_handle(little_endian_read_16(packet, 2));
    if (!s) return;
    switch (hci_event_packet_get_type(packet)) {
        case GATT_EVENT_SERVICE_QUERY_RESULT:
            if (s->step != SERVICE || size < 28) return;
            if (s->service.start_group_handle) { fail(s, "ambiguous service"); return; }
            gatt_event_service_query_result_get_service(packet, &s->service);
            break;
        case GATT_EVENT_CHARACTERISTIC_QUERY_RESULT: {
            if (s->step != CHARACTERISTICS || size < 32) return;
            gatt_client_characteristic_t chr;
            gatt_event_characteristic_query_result_get_characteristic(packet, &chr);
            if (!memcmp(chr.uuid128, write_uuid, 16) && (chr.properties & ATT_PROPERTY_WRITE)) {
                if (s->write.value_handle) { fail(s, "ambiguous write characteristic"); return; }
                s->write = chr;
            }
            if (!memcmp(chr.uuid128, notify_uuid, 16) && (chr.properties & ATT_PROPERTY_NOTIFY)) {
                if (s->notify.value_handle) { fail(s, "ambiguous notify characteristic"); return; }
                s->notify = chr;
            }
            break;
        }
        case GATT_EVENT_QUERY_COMPLETE:
            if (size < 9 || !s->busy) return;
            s->busy = false;
            if (gatt_event_query_complete_get_att_status(packet)) { fail(s, "GATT operation failed"); return; }
            if (s->step == SERVICE) {
                if (!s->service.start_group_handle) { fail(s, "private service missing"); return; }
                s->step = CHARACTERISTICS;
                check_start(s, gatt_client_discover_characteristics_for_service(packet_handler, s->handle, &s->service));
            } else if (s->step == CHARACTERISTICS) {
                if (!s->write.value_handle || !s->notify.value_handle) { fail(s, "private characteristics missing"); return; }
                s->step = SUBSCRIBE;
                gatt_client_listen_for_characteristic_value_updates(&s->listener, packet_handler, s->handle, &s->notify);
                s->listening = true;
                check_start(s, gatt_client_write_client_characteristic_configuration(
                                   packet_handler, s->handle, &s->notify, GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION));
            } else if (s->step == SUBSCRIBE) advance(s, INFO);
            break;
        case GATT_EVENT_NOTIFICATION: {
            if (size < 12 || gatt_event_notification_get_value_handle(packet) != s->notify.value_handle) return;
            uint16_t length = gatt_event_notification_get_value_length(packet);
            iine_packet_t decoded;
            if (length != size - 12 || !iine_packet_parse(gatt_event_notification_get_value(packet), length, &decoded)) return;
            if ((s->step == INFO || s->step == CAPS) && s->attempts && !s->replied &&
                decoded.opcode == 0x19 && decoded.serial == s->serial) {
                s->reply = decoded;
                s->replied = true;
            } else if (s->step >= MODE && decoded.opcode == 0x1f &&
                       iine_input_decode(decoded.payload, decoded.length, &s->input) && s->ready) {
                publish_input(s);
            }
            break;
        }
        default: break;
    }
}

void uni_bt_le_iine_start(uni_hid_device_t* device) {
    if (!device || !device->ble_iine) return;
    // Duplicate security-complete events must not restart an active GATT query.
    if (for_handle(device->conn.handle)) return;
    iine_link_t* s = NULL;
    for (size_t i = 0; i < ARRAY_SIZE(links); ++i)
        if (!links[i].device) { s = &links[i]; break; }
    if (!s) { uni_hid_device_disconnect(device); return; }
    memset(s, 0, sizeof(*s));
    s->device = device;
    s->handle = device->conn.handle;
    s->step = SERVICE;
    s->input.axes[0] = s->input.axes[1] = s->input.axes[3] = s->input.axes[4] = 128;
    s->input.hat = -1;
    btstack_run_loop_set_timer_context(&s->timer, s);
    btstack_run_loop_set_timer_handler(&s->timer, tick);
    // Allow the two bounded identity/capability attempts in addition to discovery.
    btstack_run_loop_remove_timer(&device->connection_timer);
    btstack_run_loop_set_timer(&device->connection_timer, 30000);
    btstack_run_loop_add_timer(&device->connection_timer);
    if (check_start(s, gatt_client_discover_primary_services_by_uuid128(packet_handler, s->handle, service_uuid))) {
        btstack_run_loop_set_timer(&s->timer, TICK_MS);
        btstack_run_loop_add_timer(&s->timer);
    }
}
