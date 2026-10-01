#include <assert.h>
#include <string.h>

// Exercise the production setup sequence with only its radio/platform boundary stubbed.
#include "../components/bluepad32/bt/uni_bt_setup.c"

static bool command_credit, bredr_on;
static unsigned sent, initialized, service_started;
static uint16_t opcodes[4];
bd_addr_t uni_local_bd_addr;
const hci_cmd_t hci_write_simple_pairing_mode = {0x0c56, "1"};
const hci_cmd_t hci_set_event_filter_inquiry_cod = {0x0c05, "1133"};

bool hci_can_send_command_packet_now(void) { return command_credit; }
uint8_t hci_send_cmd(const hci_cmd_t *cmd, ...) {
    assert(command_credit && sent < 4);
    opcodes[sent++] = cmd->opcode;
    return 0;
}
void gap_local_bd_addr(bd_addr_t addr) { memset(addr, 0x42, sizeof(bd_addr_t)); }
void l2cap_init(void) {}
void hci_add_event_handler(btstack_packet_callback_registration_t *cb) { assert(cb->callback); }
int hci_power_control(HCI_POWER_MODE mode) { assert(mode == HCI_POWER_ON); return 0; }
void uni_bt_packet_handler(uint8_t type, uint16_t channel, uint8_t *packet, uint16_t size) {
    uni_bt_setup_packet_handler(type, channel, packet, size);
}
bool uni_bt_bredr_is_enabled(void) { return bredr_on; }
bool uni_bt_le_is_enabled(void) { return true; }
void uni_bt_bredr_setup(void) {}
void uni_bt_le_setup(void) {}
bool uni_bt_service_is_enabled(void) { return true; }
void uni_bt_service_init(void) { service_started++; }
static void on_init_complete(void) { initialized++; }
struct uni_platform *uni_get_platform(void) {
    static struct uni_platform platform = {.on_init_complete = on_init_complete};
    return &platform;
}

static void event(uint8_t type, uint8_t value) {
    uint8_t packet[] = {type, 1, value};
    uni_bt_setup_packet_handler(HCI_EVENT_PACKET, 0, packet, sizeof(packet));
}
static void reset(bool bredr) {
    setup_fn_idx = 0;
    setup_state = SETUP_STATE_BTSTACK_IN_PROGRESS;
    command_credit = false;
    bredr_on = bredr;
    sent = initialized = service_started = 0;
    memset(uni_local_bd_addr, 0, sizeof(bd_addr_t));
    assert(uni_bt_setup() == 0);
}

int main(void) {
    // BLE needs neither SSP nor inquiry filters, even when no command credit is available.
    reset(false);
    event(BTSTACK_EVENT_STATE, HCI_STATE_WORKING);
    assert(uni_bt_setup_is_ready() && sent == 0);
    assert(initialized == 1 && service_started == 1 && uni_local_bd_addr[0] == 0x42);
    event(HCI_EVENT_COMMAND_COMPLETE, 0);
    assert(initialized == 1 && service_started == 1);

    // Preserve the original two-command path on dual-mode builds with BR/EDR enabled.
    reset(true);
    event(BTSTACK_EVENT_STATE, HCI_STATE_WORKING);
    if (IS_ENABLED(UNI_ENABLE_BREDR)) {
        assert(!uni_bt_setup_is_ready() && sent == 0 && initialized == 0);
        command_credit = true;
        event(HCI_EVENT_COMMAND_COMPLETE, 0);
        assert(sent == 1 && opcodes[0] == 0x0c56 && initialized == 0);
        event(HCI_EVENT_COMMAND_COMPLETE, 0);
        assert(sent == 2 && opcodes[1] == 0x0c05);
    } else {
        assert(sent == 0);
    }
    assert(uni_bt_setup_is_ready() && initialized == 1 && service_started == 1);
    return 0;
}
