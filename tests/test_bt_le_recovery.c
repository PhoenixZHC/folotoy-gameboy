#include <assert.h>
#include <string.h>
// Real event handlers; only radio, GATT and platform side effects are stubbed.
#include "../components/bluepad32/bt/uni_bt_le.c"

static uni_hid_device_t peer;
static unsigned disconnects, scans, deletions, pairings, key_deletes, queries, cancels;
static uint8_t connect_status;
static bool invalid_link;
uni_hid_device_t* uni_hid_device_get_instance_for_address(bd_addr_t addr) { (void)addr; return &peer; }
uni_hid_device_t* uni_hid_device_get_instance_for_connection_handle(hci_con_handle_t handle) {
    return handle == peer.conn.handle ? &peer : NULL;
}
uni_hid_device_t* uni_hid_device_get_first_device_with_state(uni_bt_conn_state_t value) {
    return peer.conn.state == value ? &peer : NULL;
}
uni_hid_device_t* uni_hid_device_get_instance_for_hids_cid(uint16_t cid) { (void)cid; return &peer; }
void uni_hid_device_on_connected(uni_hid_device_t* d, bool connected) { (void)d; assert(!connected); }
void uni_hid_device_delete(uni_hid_device_t* d) { (void)d; deletions++; }
void uni_hid_device_disconnect(uni_hid_device_t* d) { hog_disconnect(d->conn.handle); }
void uni_hid_device_set_connection_handle(uni_hid_device_t* d, hci_con_handle_t handle) { d->conn.handle = handle; }
void uni_hid_device_set_cod(uni_hid_device_t* d, uint32_t cod) { d->cod = cod; }
void uni_hid_device_set_hid_descriptor(uni_hid_device_t* d, const uint8_t* map, int len) { (void)d; (void)map; (void)len; }
void uni_hid_device_guess_controller_type_from_pid_vid(uni_hid_device_t* d) { (void)d; }
void uni_hid_device_connect(uni_hid_device_t* d) { (void)d; }
void uni_hid_device_set_ready(uni_hid_device_t* d) { (void)d; }
void uni_hid_parse_input_report(uni_hid_device_t* d, const uint8_t* p, uint16_t len) { (void)d; (void)p; (void)len; }
void uni_hid_device_process_controller(uni_hid_device_t* d) { (void)d; }
void gap_stop_scan(void) {}
void gap_start_scan(void) { scans++; }
uint8_t gap_connect(const bd_addr_t addr, bd_addr_type_t type) { (void)addr; (void)type; return connect_status; }
uint8_t gap_disconnect(hci_con_handle_t handle) { (void)handle; disconnects++; return 0; }
gap_connection_type_t gap_get_connection_type(hci_con_handle_t handle) {
    (void)handle; return invalid_link ? GAP_CONNECTION_INVALID : GAP_CONNECTION_LE;
}
uint8_t gap_connect_cancel(void) { cancels++; return 0; }
void gap_delete_bonding(bd_addr_type_t type, bd_addr_t addr) { (void)addr; assert(type == 1); key_deletes++; }
uint8_t hids_client_disconnect(uint16_t cid) { (void)cid; return 0; }
uint8_t hids_client_connect(hci_con_handle_t handle, btstack_packet_handler_t callback, hid_protocol_mode_t mode, uint16_t* cid) {
    (void)handle; (void)callback; (void)mode; *cid = 1; return 0;
}
const uint8_t* hids_client_descriptor_storage_get_descriptor_data(uint16_t cid, uint8_t index) { (void)cid; (void)index; return NULL; }
uint16_t hids_client_descriptor_storage_get_descriptor_len(uint16_t cid, uint8_t index) { (void)cid; (void)index; return 0; }
uint8_t device_information_service_client_query(hci_con_handle_t handle, btstack_packet_handler_t callback) {
    (void)handle; (void)callback; queries++; return 0;
}
void sm_request_pairing(hci_con_handle_t handle) { assert(handle == peer.conn.handle); pairings++; }
void sm_just_works_confirm(hci_con_handle_t handle) { (void)handle; }
void sm_numeric_comparison_confirm(hci_con_handle_t handle) { (void)handle; }
void sm_add_event_handler(btstack_packet_callback_registration_t* cb) { (void)cb; }
void sm_init(void) {}
void sm_set_io_capabilities(io_capability_t cap) { (void)cap; }
void sm_set_authentication_requirements(uint8_t auth) { (void)auth; }
void gatt_client_init(void) {}
void hids_client_init(uint8_t* buf, uint16_t len) { (void)buf; (void)len; }
void device_information_service_client_init(void) {}
void le_device_db_init(void) {}
int le_device_db_max_count(void) { return 0; }
void le_device_db_info(int index, int* type, bd_addr_t addr, sm_key_t key) {
    (void)index; (void)type; (void)addr; (void)key;
}
void gap_set_scan_parameters(uint8_t type, uint16_t interval, uint16_t window) {
    (void)type; (void)interval; (void)window;
}
void uni_bt_le_adv_reset(uni_bt_le_adv_cache_t* cache) { memset(cache, 0, sizeof(*cache)); }
void uni_property_set(uni_property_idx_t idx, uni_property_value_t value) { (void)idx; (void)value; }
uni_property_value_t uni_property_get(uni_property_idx_t idx) { (void)idx; return (uni_property_value_t){0}; }

static void security(uint8_t event, uint8_t status) {
    uint8_t packet[13] = {event, 11, 7, 0, 1, 6, 5, 4, 3, 2, 1, status, 0};
    uni_sm_packet_handler(HCI_EVENT_PACKET, 0, packet, sizeof(packet));
}
int main(void) {
    peer.conn.handle = 7;
    peer.conn.protocol = UNI_BT_CONN_PROTOCOL_BLE;
    peer.conn.state = UNI_BT_CONN_STATE_DEVICE_DISCOVERED;
    is_scanning = true;
    uint8_t failed[21] = {HCI_EVENT_LE_META, 19, HCI_SUBEVENT_LE_CONNECTION_COMPLETE, ERROR_CODE_CONNECTION_TIMEOUT};
    uni_bt_le_on_hci_event_le_meta(failed, sizeof(failed));
    assert(deletions == 1 && scans == 1 && !pairings && peer.conn.handle == 7);
    connect_status = ERROR_CODE_COMMAND_DISALLOWED;
    hog_connect(peer.conn.btaddr, 0);
    assert(deletions == 2 && scans == 2 && !pairings);
    security(SM_EVENT_REENCRYPTION_COMPLETE, ERROR_CODE_PIN_OR_KEY_MISSING);
    assert(key_deletes == 1 && pairings == 1 && !disconnects);
    security(SM_EVENT_REENCRYPTION_COMPLETE, ERROR_CODE_PIN_OR_KEY_MISSING);
    assert(key_deletes == 1 && pairings == 1 && disconnects == 1);
    security(SM_EVENT_PAIRING_COMPLETE, ERROR_CODE_AUTHENTICATION_FAILURE);
    assert(disconnects == 2 && !queries);
    security(SM_EVENT_PAIRING_COMPLETE, ERROR_CODE_SUCCESS);
    assert(queries == 1);
    security(SM_EVENT_REENCRYPTION_COMPLETE, ERROR_CODE_UNSPECIFIED_ERROR);
    assert(disconnects == 3);
    invalid_link = true;
    hog_disconnect(UNI_BT_CONN_HANDLE_INVALID);
    assert(cancels == 1);
    return 0;
}
