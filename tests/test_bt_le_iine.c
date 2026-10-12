#include <assert.h>
#include <stdio.h>
#include <string.h>
// Exercise the production state machine; stub only BTstack/radio/platform I/O.
#include "../components/bluepad32/bt/uni_bt_le_iine.c"

static uint32_t clock_ms;
static unsigned starts, writes, disconnected, connected, ready, reports, listeners;
static uint8_t request_status;
static bool reject_ready;
static iine_packet_t sent;
static const uint8_t* borrowed;
static uint8_t saved_wire[20];
static uint16_t saved_length;
static uni_hid_device_t peer;
static uni_gamepad_t observed;

uint32_t btstack_run_loop_get_time_ms(void) { return clock_ms; }
void btstack_run_loop_set_timer_context(btstack_timer_source_t* t, void* context) { t->context = context; }
void* btstack_run_loop_get_timer_context(btstack_timer_source_t* t) { return t->context; }
void btstack_run_loop_set_timer_handler(btstack_timer_source_t* t, void (*handler)(btstack_timer_source_t*)) { t->process = handler; }
void btstack_run_loop_set_timer(btstack_timer_source_t* t, uint32_t ms) { t->timeout = clock_ms + ms; }
void btstack_run_loop_add_timer(btstack_timer_source_t* t) { (void)t; }
int btstack_run_loop_remove_timer(btstack_timer_source_t* t) { (void)t; return 1; }
void uni_hid_device_disconnect(uni_hid_device_t* d) { assert(d == &peer); disconnected++; uni_bt_le_iine_reset(d); }
void uni_hid_device_connect(uni_hid_device_t* d) { assert(d == &peer); connected++; }
bool uni_hid_device_set_ready_complete(uni_hid_device_t* d) {
    assert(d == &peer);
    if (reject_ready) { uni_bt_le_iine_reset(d); return false; }
    ready++; return true;
}
void uni_hid_device_process_controller(uni_hid_device_t* d) {
    assert(d == &peer && d->controller.klass == UNI_CONTROLLER_CLASS_GAMEPAD);
    observed = d->controller.gamepad;
    reports++;
    d->controller.gamepad.buttons = 0; // Model a platform mapping; delta state must survive.
}
uint8_t gatt_client_discover_primary_services_by_uuid128(btstack_packet_handler_t cb, hci_con_handle_t h, const uint8_t* uuid) {
    assert(cb == packet_handler && h == peer.conn.handle && !memcmp(uuid, service_uuid, 16));
    starts++; return request_status;
}
uint8_t gatt_client_discover_characteristics_for_service(btstack_packet_handler_t cb, hci_con_handle_t h, gatt_client_service_t* svc) {
    assert(cb == packet_handler && h == peer.conn.handle && svc->start_group_handle == 1); return request_status;
}
uint8_t gatt_client_write_client_characteristic_configuration(btstack_packet_handler_t cb, hci_con_handle_t h,
                                                             gatt_client_characteristic_t* chr, uint16_t config) {
    assert(cb == packet_handler && h == peer.conn.handle && chr->value_handle == 5 && config == 1); return request_status;
}
void gatt_client_listen_for_characteristic_value_updates(gatt_client_notification_t* n, btstack_packet_handler_t cb,
                                                        hci_con_handle_t h, gatt_client_characteristic_t* chr) {
    (void)n; assert(cb == packet_handler && h == peer.conn.handle && chr->value_handle == 5); listeners++;
}
void gatt_client_stop_listening_for_characteristic_value_updates(gatt_client_notification_t* n) {
    (void)n; assert(listeners); listeners--;
}
uint8_t gatt_client_write_value_of_characteristic(btstack_packet_handler_t cb, hci_con_handle_t h,
                                                 uint16_t value_handle, uint16_t length, uint8_t* value) {
    assert(cb == packet_handler && h == peer.conn.handle && value_handle == 3);
    assert(iine_packet_parse(value, length, &sent));
    assert(iine_request_allowed(sent.opcode, sent.payload, sent.length));
    borrowed = value; saved_length = length; memcpy(saved_wire, value, length);
    writes++; return request_status;
}
// Same event layout as vendored BTstack, with no dependency on the whole GATT client.
void gatt_client_deserialize_service(const uint8_t* p, int offset, gatt_client_service_t* svc) {
    svc->start_group_handle = little_endian_read_16(p, offset);
    svc->end_group_handle = little_endian_read_16(p, offset + 2);
    reverse_128(p + offset + 4, svc->uuid128);
}
void gatt_client_deserialize_characteristic(const uint8_t* p, int offset, gatt_client_characteristic_t* chr) {
    memset(chr, 0, sizeof(*chr));
    chr->start_handle = little_endian_read_16(p, offset);
    chr->value_handle = little_endian_read_16(p, offset + 2);
    chr->end_handle = little_endian_read_16(p, offset + 4);
    chr->properties = little_endian_read_16(p, offset + 6);
    reverse_128(p + offset + 8, chr->uuid128);
}

static void event(uint8_t* p, uint16_t length) {
    p[1] = length - 2;
    little_endian_store_16(p, 2, peer.conn.handle);
    packet_handler(HCI_EVENT_PACKET, 0, p, length);
}
static void complete(uint8_t status) {
    if (borrowed) assert(!memcmp(borrowed, saved_wire, saved_length));
    borrowed = NULL;
    uint8_t p[9] = {GATT_EVENT_QUERY_COMPLETE}; p[8] = status; event(p, sizeof(p));
}
static void elapsed(uint32_t ms) {
    clock_ms += ms;
    tick(&links[0].timer);
}
static void notify(uint8_t opcode, uint8_t serial, const uint8_t* payload, size_t length) {
    uint8_t p[32] = {GATT_EVENT_NOTIFICATION};
    little_endian_store_16(p, 8, 5);
    size_t size = iine_packet_build(p + 12, 20, opcode, serial, 0x43, payload, length);
    little_endian_store_16(p, 10, size);
    event(p, 12 + size);
}
static void discover(void) {
    borrowed = NULL;
    uni_bt_le_iine_reset(&peer);
    memset(&peer, 0, sizeof(peer)); peer.ble_iine = true; peer.conn.handle = 7;
    uni_bt_le_iine_start(&peer);
    unsigned count = starts;
    uni_bt_le_iine_start(&peer);
    assert(starts == count); // Duplicate security event.
    uint8_t service[28] = {GATT_EVENT_SERVICE_QUERY_RESULT};
    little_endian_store_16(service, 8, 1); little_endian_store_16(service, 10, 9);
    reverse_128(service_uuid, service + 12); event(service, sizeof(service)); complete(0);
    for (unsigned i = 0; i < 2; ++i) {
        uint8_t chr[32] = {GATT_EVENT_CHARACTERISTIC_QUERY_RESULT};
        little_endian_store_16(chr, 8, i ? 4 : 2);
        little_endian_store_16(chr, 10, i ? 5 : 3);
        little_endian_store_16(chr, 12, i ? 9 : 3);
        little_endian_store_16(chr, 14, i ? ATT_PROPERTY_NOTIFY : ATT_PROPERTY_WRITE);
        reverse_128(i ? notify_uuid : write_uuid, chr + 16); event(chr, sizeof(chr));
    }
    complete(0); assert(listeners == 1); complete(0); elapsed(50);
    assert(sent.opcode == 0x91);
}
static void identify(bool supported) {
    uint8_t identity[] = {7,0x10,2,0x20,0x27,2,9,0x51};
    if (!supported) identity[4] = 0x28;
    notify(0x19, sent.serial + 1, identity, sizeof(identity));
    assert(!links[0].replied);
    notify(0x19, sent.serial, identity, sizeof(identity)); // Reply can precede ATT write ACK.
    elapsed(50); assert(links[0].step == INFO);
    complete(0); elapsed(50);
}
static void enter_test(void) {
    elapsed(50); assert(sent.opcode == 0xb0); complete(0);
    // Both missing capability replies fall back, with bounded retries.
    elapsed(4500); assert(sent.opcode == 0xb0); complete(0);
    elapsed(4500); elapsed(50); assert(sent.opcode == 0x1d && sent.length == 3); complete(0);
    elapsed(499); assert(links[0].step == DELAY);
    elapsed(1); assert(sent.length == 6 && links[0].step == TEST); complete(0);
    elapsed(50); assert(links[0].ready && ready && connected);
    assert(!observed.buttons && !observed.dpad && !observed.axis_x && !observed.axis_y);
}

int main(void) {
    discover(); identify(true); enter_test();
    const uint8_t a[] = {2,1,1}, b[] = {2,1,2}, release[] = {4,0,1,0,2};
    notify(0x1f, 0, a, sizeof(a)); assert(observed.buttons == BUTTON_A);
    notify(0x1f, 0, b, sizeof(b)); assert(observed.buttons == (BUTTON_A | BUTTON_B));
    unsigned before = reports;
    elapsed(2000); assert(sent.length == 6); complete(0);
    assert(reports == before && observed.buttons == (BUTTON_A | BUTTON_B)); // Held input survives silence.
    const uint8_t invalid[] = {4,0,1,1,0xfe};
    notify(0x1f, 0, invalid, sizeof(invalid)); assert(reports == before);
    uint8_t truncated[] = {GATT_EVENT_NOTIFICATION, 10, 7, 0, 0, 0, 0, 0, 5, 0, 20, 0};
    packet_handler(HCI_EVENT_PACKET, 0, truncated, sizeof(truncated)); assert(reports == before);
    notify(0x1f, 0, release, sizeof(release)); assert(!observed.buttons);
    const uint8_t aux[] = {8,1,0x5d,1,0x5e,1,0x0b,1,0x0c};
    notify(0x1f, 0, aux, sizeof(aux));
    assert(observed.buttons == (BUTTON_THUMB_L | BUTTON_THUMB_R));
    assert(observed.misc_buttons == (MISC_BUTTON_SELECT | MISC_BUTTON_START));
    const uint8_t axes[] = {12,1,18,0,255,0,255,1,19,255,0,255,0};
    notify(0x1f, 0, axes, sizeof(axes));
    assert(observed.axis_x == -512 && observed.axis_y == 508 && observed.axis_rx == 508 && observed.axis_ry == -512);
    const uint8_t directions[] = {4,1,14,1,16}, triggers[] = {8,1,7,255,0,1,8,128,0};
    notify(0x1f, 0, directions, sizeof(directions)); assert(observed.dpad == (DPAD_UP | DPAD_LEFT));
    notify(0x1f, 0, triggers, sizeof(triggers)); assert(observed.brake == 1023 && observed.throttle == 513);
    uni_bt_le_iine_reset(&peer); assert(!listeners && !links[0].device);
    before = reports; elapsed(5000); notify(0x1f, 0, a, sizeof(a)); assert(reports == before);

    discover(); identify(false); assert(disconnected == 1 && !listeners && !links[0].device);
    discover(); complete(0); elapsed(4500); assert(sent.opcode == 0x91); complete(0);
    elapsed(4500); assert(disconnected == 2 && !listeners);
    discover(); elapsed(4501); assert(disconnected == 3 && !listeners); // ATT timeout.
    discover(); complete(ATT_ERROR_INSUFFICIENT_AUTHENTICATION); assert(disconnected == 4 && !listeners);
    borrowed = NULL; request_status = ERROR_CODE_COMMAND_DISALLOWED;
    uni_bt_le_iine_start(&peer); assert(disconnected == 5 && !links[0].device);
    request_status = 0;
    discover(); identify(true); enter_test(); // Reconnect starts with neutral state.
    assert(!observed.buttons && !observed.misc_buttons && !observed.dpad && !observed.axis_x);
    uni_bt_le_iine_reset(&peer);
    // No service must fail rather than marking an unknown device ready.
    uni_bt_le_iine_start(&peer); complete(0); assert(disconnected == 6);
    discover(); identify(true);
    // A platform cancellation while completing setup must not resurrect timers/input.
    elapsed(50); complete(0);
    notify(0x19, sent.serial, NULL, 0); elapsed(50); elapsed(50); complete(0);
    const uint8_t early[] = {2,1,1};
    notify(0x1f, 0, early, sizeof(early));
    assert(links[0].input.buttons == 1); // Changes between mode/test writes are retained.
    elapsed(500); complete(0); reject_ready = true; before = reports; elapsed(50);
    assert(!links[0].device && !listeners && reports == before);
    puts("IINE BTstack: discovery, identity, retries, keepalive, delta mapping and reconnect passed");
    return 0;
}
