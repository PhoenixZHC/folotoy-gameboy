#include <assert.h>
#include <string.h>

#include "bt/uni_bt_le_advertisement.h"
#include "gamepad_discovery.h"

static const uint8_t address[6] = {1, 2, 3, 4, 5, 6};
static const uint8_t gamepad[] = {3, 0x19, 0xc4, 0x03};
static const uint8_t name[] = {5, 0x09, 'P', 'a', 'd', 'A'};
static const uint8_t short_name[] = {3, 0x08, 'P', 'a'};

static void test_split_reports(uint16_t appearance, uint16_t cod) {
    const uint8_t input_ad[] = {3, 0x19, appearance & 0xff, appearance >> 8};
    uni_bt_le_adv_cache_t cache = {0};
    gamepad_discovery_t discovery = {0};
    const uni_bt_le_adv_entry_t* entry = uni_bt_le_adv_observe(&cache, address, 0, input_ad, sizeof(input_ad), 0);
    assert(entry && entry->appearance == appearance && !entry->name[0]);
    assert(uni_bt_le_adv_cod(entry) == cod);
    assert(gamepad_discovery_observe(&discovery, entry->address, cod, entry->name));
    assert(gamepad_discovery_select(&discovery, 0));
    entry = uni_bt_le_adv_observe(&cache, address, 0, name, sizeof(name), 10);
    assert(entry->appearance == appearance && strcmp(entry->name, "PadA") == 0);
    assert(gamepad_discovery_observe(&discovery, entry->address, cod, entry->name));
    assert(discovery.count == 1 && strcmp(discovery.candidates[0].name, "PadA") == 0);
    assert(gamepad_discovery_should_connect(&discovery, entry->address));
    entry = uni_bt_le_adv_observe(&cache, address, 0, input_ad, sizeof(input_ad), 20);
    assert(strcmp(entry->name, "PadA") == 0);

    uni_bt_le_adv_reset(&cache);
    entry = uni_bt_le_adv_observe(&cache, address, 0, name, sizeof(name), 30);
    assert(entry->appearance == 0);
    entry = uni_bt_le_adv_observe(&cache, address, 0, input_ad, sizeof(input_ad), 40);
    assert(entry->appearance == appearance && strcmp(entry->name, "PadA") == 0);
    entry = uni_bt_le_adv_observe(&cache, address, 0, short_name, sizeof(short_name), 50);
    assert(strcmp(entry->name, "PadA") == 0);

    uni_bt_le_adv_reset(&cache);
    entry = uni_bt_le_adv_observe(&cache, address, 0, short_name, sizeof(short_name), 60);
    assert(strcmp(entry->name, "Pa") == 0);
    entry = uni_bt_le_adv_observe(&cache, address, 0, name, sizeof(name), 70);
    assert(strcmp(entry->name, "PadA") == 0);
}

static void test_isolation_and_expiry(void) {
    uni_bt_le_adv_cache_t cache = {0};
    const uint8_t other[6] = {1, 2, 3, 4, 5, 7};
    uni_bt_le_adv_observe(&cache, address, 0, gamepad, sizeof(gamepad), 0);
    const uni_bt_le_adv_entry_t* entry = uni_bt_le_adv_observe(&cache, other, 0, name, sizeof(name), 1);
    assert(entry->appearance == 0);
    entry = uni_bt_le_adv_observe(&cache, address, 1, name, sizeof(name), 2);
    assert(entry->appearance == 0);
    entry = uni_bt_le_adv_observe(&cache, address, 0, name, sizeof(name), 3);
    assert(entry->appearance == 0x03c4);
    entry = uni_bt_le_adv_observe(&cache, address, 0, name, sizeof(name), 3 + UNI_BT_LE_ADV_TTL_MS);
    assert(entry->appearance == 0);
    uni_bt_le_adv_reset(&cache);
    uni_bt_le_adv_observe(&cache, address, 0, gamepad, sizeof(gamepad), UINT32_MAX - 100);
    entry = uni_bt_le_adv_observe(&cache, address, 0, name, sizeof(name), 100);
    assert(entry->appearance == 0x03c4);
    entry = uni_bt_le_adv_observe(&cache, address, 0, name, sizeof(name), 100 + UNI_BT_LE_ADV_TTL_MS);
    assert(entry->appearance == 0);

    uni_bt_le_adv_reset(&cache);
    for (uint8_t i = 0; i < UNI_BT_LE_ADV_CACHE_SIZE; i++) {
        const uint8_t peer[6] = {0, 0, 0, 0, 0, i};
        uni_bt_le_adv_observe(&cache, peer, 0, gamepad, sizeof(gamepad), i);
    }
    const uint8_t oldest[6] = {0};
    const uint8_t second[6] = {0, 0, 0, 0, 0, 1};
    uni_bt_le_adv_observe(&cache, oldest, 0, NULL, 0, 50); // Refresh oldest peer.
    uni_bt_le_adv_observe(&cache, address, 0, name, sizeof(name), 51);
    entry = uni_bt_le_adv_observe(&cache, oldest, 0, NULL, 0, 52);
    assert(entry->appearance == 0x03c4);
    entry = uni_bt_le_adv_observe(&cache, second, 0, name, sizeof(name), 53);
    assert(entry->appearance == 0); // Least recent peer was evicted.
}

static void test_packet_bounds(void) {
    uni_bt_le_adv_cache_t cache = {0};
    const uint8_t combined[] = {3, 0x19, 0xc4, 3, 5, 9, 'P', 'a', 'd', 'A', 0};
    const uni_bt_le_adv_entry_t* entry = uni_bt_le_adv_observe(&cache, address, 0, combined, sizeof(combined), 0);
    assert(entry->appearance == 0x03c4 && strcmp(entry->name, "PadA") == 0);
    const uint8_t truncated[] = {3, 0x19, 0xc1, 3, 5, 9, 'X'};
    assert(!uni_bt_le_adv_observe(&cache, address, 0, truncated, sizeof(truncated), 1));
    const uint8_t bad_appearance[] = {2, 0x19, 0xc1};
    assert(!uni_bt_le_adv_observe(&cache, address, 0, bad_appearance, sizeof(bad_appearance), 2));
    entry = uni_bt_le_adv_observe(&cache, address, 0, NULL, 0, 3);
    assert(entry->appearance == 0x03c4 && strcmp(entry->name, "PadA") == 0);
    const uint8_t empty_name[] = {1, 9};
    entry = uni_bt_le_adv_observe(&cache, address, 0, empty_name, sizeof(empty_name), 4);
    assert(strcmp(entry->name, "PadA") == 0);
    uint8_t long_name[82];
    memset(long_name, 'a', sizeof(long_name));
    long_name[0] = 81;
    long_name[1] = 9;
    entry = uni_bt_le_adv_observe(&cache, address, 0, long_name, sizeof(long_name), 5);
    assert(strlen(entry->name) == UNI_BT_LE_ADV_NAME_SIZE - 1);
    long_name[64] = 0xe9;
    long_name[65] = 0x94;
    long_name[66] = 0xae;
    entry = uni_bt_le_adv_observe(&cache, address, 0, long_name, sizeof(long_name), 6);
    assert(strlen(entry->name) == 62);
    uni_bt_le_adv_reset(&cache);
    entry = uni_bt_le_adv_observe(&cache, address, 0, name, sizeof(name), 7);
    assert(entry->appearance == 0);
}

static void test_hid_service_fallback(void) {
    const uint8_t hid[] = {5, 0x03, 0x0f, 0x18, 0x12, 0x18};
    const uint8_t partial_hid[] = {3, 0x02, 0x12, 0x18};
    const uint8_t hid128[] = {17, 0x07,
        0xfb,0x34,0x9b,0x5f,0x80,0,0,0x80,0,0x10,0,0,0x12,0x18,0,0};
    const uint8_t generic[] = {3, 0x19, 0xc0, 3};
    const uint8_t mouse[] = {3, 0x19, 0xc2, 3};
    const uint8_t remote[] = {3, 0x19, 0x80, 1};
    const uint8_t battery[] = {3, 0x03, 0x0f, 0x18};
    const uint8_t solicitation[] = {3, 0x14, 0x12, 0x18};
    const uint8_t bad16[] = {2, 0x03, 0x12};
    const uint8_t bad128[] = {3, 0x07, 0x12, 0x18};
    uni_bt_le_adv_cache_t cache = {0};
    gamepad_discovery_t discovery = {0};
    const uni_bt_le_adv_entry_t* entry = uni_bt_le_adv_observe(&cache, address, 0, name, sizeof(name), 0);
    assert(!uni_bt_le_adv_cod(entry));
    entry = uni_bt_le_adv_observe(&cache, address, 0, hid, sizeof(hid), 1);
    assert(entry->hid_service && uni_bt_le_adv_cod(entry) == 0x0500);
    assert(gamepad_discovery_observe(&discovery, entry->address, uni_bt_le_adv_cod(entry), entry->name));
    assert(discovery.candidates[0].type_pending && discovery.count == 1);
    assert(!gamepad_discovery_should_connect(&discovery, address));
    assert(gamepad_discovery_select(&discovery, 0));
    assert(gamepad_discovery_should_connect(&discovery, address));
    entry = uni_bt_le_adv_observe(&cache, address, 0, generic, sizeof(generic), 2);
    assert(uni_bt_le_adv_cod(entry) == 0x0500);
    entry = uni_bt_le_adv_observe(&cache, address, 1, name, sizeof(name), 3);
    assert(!uni_bt_le_adv_cod(entry)); // Address types cannot share UUID evidence.
    const uint8_t other[6] = {1,2,3,4,5,7};
    entry = uni_bt_le_adv_observe(&cache, other, 0, name, sizeof(name), 3);
    assert(!uni_bt_le_adv_cod(entry));
    entry = uni_bt_le_adv_observe(&cache, address, 0, mouse, sizeof(mouse), 4);
    assert(uni_bt_le_adv_cod(entry) == 0x0580);
    assert(!gamepad_discovery_observe(&discovery, address, uni_bt_le_adv_cod(entry), entry->name));
    entry = uni_bt_le_adv_observe(&cache, address, 0, remote, sizeof(remote), 5);
    assert(!uni_bt_le_adv_cod(entry)); // Explicitly different types are not overridden.
    uni_bt_le_adv_reset(&cache);
    entry = uni_bt_le_adv_observe(&cache, address, 0, hid128, sizeof(hid128), 6);
    assert(uni_bt_le_adv_cod(entry) == 0x0500);
    entry = uni_bt_le_adv_observe(&cache, address, 0, name, sizeof(name), 7);
    assert(uni_bt_le_adv_cod(entry) == 0x0500 && !strcmp(entry->name, "PadA"));
    entry = uni_bt_le_adv_observe(&cache, address, 0, name, sizeof(name), 7 + UNI_BT_LE_ADV_TTL_MS);
    assert(!uni_bt_le_adv_cod(entry));
    entry = uni_bt_le_adv_observe(&cache, address, 0, partial_hid, sizeof(partial_hid), 6000);
    assert(uni_bt_le_adv_cod(entry) == 0x0500);
    uni_bt_le_adv_reset(&cache);
    assert(!uni_bt_le_adv_observe(&cache, address, 0, bad16, sizeof(bad16), 6001));
    assert(!uni_bt_le_adv_observe(&cache, address, 0, bad128, sizeof(bad128), 6002));
    entry = uni_bt_le_adv_observe(&cache, address, 0, battery, sizeof(battery), 6003);
    assert(!uni_bt_le_adv_cod(entry));
    entry = uni_bt_le_adv_observe(&cache, address, 0, solicitation, sizeof(solicitation), 6004);
    assert(!uni_bt_le_adv_cod(entry));
    entry = uni_bt_le_adv_observe(&cache, address, 0, generic, sizeof(generic), 6005);
    assert(!uni_bt_le_adv_cod(entry));
}

int main(void) {
    test_split_reports(0x03c4, 0x0508); // Gamepad.
    test_split_reports(0x03c3, 0x0504); // Joystick uses the same discovery/selection flow.
    test_isolation_and_expiry();
    test_packet_bounds();
    test_hid_service_fallback();
    return 0;
}
