#include <assert.h>
#include <string.h>

#include "gamepad_discovery.h"

static void test_fresh_scan(void) {
    gamepad_discovery_t state = {0};
    for (uint8_t n = 0; n < GAMEPAD_DISCOVERY_MAX; n++) {
        const uint8_t addr[6] = {1, 2, 3, 4, 5, n};
        assert(gamepad_discovery_observe(&state, addr, 0x0540, "Old keyboard"));
    }
    const uint8_t new_address[6] = {1, 2, 3, 4, 5, 99};
    assert(!gamepad_discovery_observe(&state, new_address, 0x0540, "New keyboard"));
    uint32_t revision = state.revision;
    gamepad_discovery_begin_scan(&state);
    assert(state.count == 0 && state.revision != revision);
    assert(!gamepad_discovery_select(&state, 0));
    assert(gamepad_discovery_observe(&state, new_address, 0x0540, NULL));
    assert(state.count == 1 && state.candidates[0].name[0] == 0);
    assert(gamepad_discovery_select(&state, 0));
    gamepad_discovery_passkey(&state, new_address, 42);
    revision = state.revision;
    gamepad_discovery_begin_scan(&state);
    assert(state.count == 1 && state.revision == revision);
    assert(gamepad_discovery_should_connect(&state, new_address));
    assert(state.passkey_visible && state.passkey == 42);
    gamepad_discovery_pairing_done(&state, new_address, false);
    gamepad_discovery_begin_scan(&state);
    assert(gamepad_discovery_should_connect(&state, new_address));
    assert(state.pairing_failed); // Preserve the selected reconnect target.
    gamepad_discovery_clear_selection(&state);
    assert(!state.selected && state.count == 0);
    assert(!state.passkey_visible && !state.passkey && !state.pairing_failed);
    assert(!gamepad_discovery_should_connect(&state, new_address));
    gamepad_discovery_passkey(&state, new_address, 123456);
    gamepad_discovery_pairing_done(&state, new_address, false);
    assert(!state.passkey_visible && !state.pairing_failed);
    gamepad_discovery_begin_scan(&state); // Reenter after cancellation.
    const uint8_t other_address[6] = {9, 8, 7, 6, 5, 4};
    assert(gamepad_discovery_observe(&state, other_address, 0x0540, "Other keyboard"));
    assert(state.count == 1);
    assert(gamepad_discovery_select(&state, 0));
    assert(gamepad_discovery_should_connect(&state, other_address));
    assert(!gamepad_discovery_should_connect(&state, new_address));
    gamepad_discovery_clear_selection(&state);
    assert(gamepad_discovery_observe(&state, new_address, 0x0540, NULL));
    gamepad_discovery_clear_selection(&state); // Also clears an unselected full/old list.
    assert(state.count == 0);
}

int main(void) {
    test_fresh_scan();
    gamepad_discovery_t state = {0};
    const uint8_t xbox[6] = {0x44, 0x16, 0x22, 0x01, 0x02, 0x03};
    const uint8_t other[6] = {0x44, 0x16, 0x22, 0x01, 0x02, 0x04};

    assert(!gamepad_discovery_observe(&state, xbox, 0x0504, ""));
    assert(!gamepad_discovery_observe(&state, xbox, 0x0508, "Other Pad"));
    assert(state.count == 0);
    assert(gamepad_discovery_observe(&state, xbox, 0x0508, ""));
    assert(state.count == 1);
    assert(!gamepad_discovery_should_connect(&state, xbox));
    uint32_t revision = state.revision;
    assert(gamepad_discovery_observe(&state, xbox, 0x0508, "Xbox Wireless Controller"));
    assert(state.count == 1 && state.revision == revision + 1);
    assert(strcmp(state.candidates[0].name, "Xbox Wireless Controller") == 0);
    assert(gamepad_discovery_observe(&state, other, 0x0508, ""));
    assert(state.count == 2);
    assert(gamepad_discovery_select(&state, 0));
    assert(gamepad_discovery_should_connect(&state, xbox));
    assert(!gamepad_discovery_should_connect(&state, other));
    gamepad_discovery_clear_selection(&state);
    assert(!gamepad_discovery_should_connect(&state, xbox));
    assert(state.count == 0);
    assert(!gamepad_discovery_select(&state, 2));
    const uint8_t keyboard[6] = {0x44, 0x16, 0x22, 0x01, 0x02, 0x05};
    assert(gamepad_discovery_observe(&state, keyboard, 0x0540, "MCHOSE Keyboard"));
    assert(state.count == 1 && state.candidates[0].keyboard);
    assert(gamepad_discovery_observe(&state, keyboard, 0x0540, "Another Brand"));
    assert(state.count == 1);
    assert(!gamepad_discovery_observe(&state, keyboard, 0x0580, "Mouse"));
    assert(!gamepad_discovery_observe(&state, keyboard, 0x0140, "Not a peripheral"));
    assert(gamepad_discovery_select(&state, 0));
    gamepad_discovery_passkey(&state, xbox, 123456);
    assert(!state.passkey_visible);
    gamepad_discovery_passkey(&state, keyboard, 1000000);
    assert(!state.passkey_visible);
    gamepad_discovery_passkey(&state, keyboard, 42);
    assert(state.passkey_visible && state.passkey == 42 && !state.pairing_failed);
    gamepad_discovery_pairing_done(&state, xbox, false);
    assert(state.passkey_visible && !state.pairing_failed);
    gamepad_discovery_pairing_done(&state, keyboard, false);
    assert(!state.passkey_visible && state.passkey == 0 && state.pairing_failed);
    gamepad_discovery_passkey(&state, keyboard, 0);
    assert(state.passkey_visible && state.passkey == 0 && !state.pairing_failed);
    gamepad_discovery_pairing_done(&state, keyboard, true);
    assert(!state.passkey_visible && !state.pairing_failed);
    gamepad_discovery_passkey(&state, keyboard, 123456);
    gamepad_discovery_clear_selection(&state);
    assert(!state.passkey_visible && state.passkey == 0);
    gamepad_discovery_passkey(&state, keyboard, 123456);
    assert(!state.passkey_visible); // Late event from the cancelled connection.
    assert(gamepad_discovery_observe(&state, xbox, 0x0508, "Xbox"));
    assert(!state.candidates[0].keyboard);
    assert(gamepad_discovery_select(&state, 0));
    gamepad_discovery_pairing_done(&state, keyboard, false);
    assert(!state.pairing_failed);
    assert(gamepad_discovery_observe(&state, keyboard, 0x0540, "Keyboard"));
    for (uint8_t n = 6; state.count < GAMEPAD_DISCOVERY_MAX; n++) {
        const uint8_t addr[6] = {1, 2, 3, 4, 5, n};
        assert(gamepad_discovery_observe(&state, addr, 0x0540, NULL));
    }
    const uint8_t overflow[6] = {1, 2, 3, 4, 5, 99};
    assert(!gamepad_discovery_observe(&state, overflow, 0x0540, "Keyboard"));
    assert(gamepad_discovery_observe(&state, keyboard, 0x0540, "Known keyboard"));
    char short_name[6];
    gamepad_copy_name(short_name, sizeof(short_name), "AB\xe9\x94\xae\xe7\x9b\x98");
    assert(strcmp(short_name, "AB\xe9\x94\xae") == 0);
    gamepad_copy_name(short_name, 5, "AB\xe9\x94\xae\xe7\x9b\x98");
    assert(strcmp(short_name, "AB") == 0);
    gamepad_copy_name(short_name, 1, "Keyboard");
    assert(short_name[0] == 0);
    const char *long_name = "Very long generic BLE keyboard name beyond buffer";
    assert(gamepad_discovery_observe(&state, keyboard, 0x0540, long_name));
    revision = state.revision;
    assert(gamepad_discovery_observe(&state, keyboard, 0x0540, long_name));
    assert(state.revision == revision); // Long names must not trigger endless UI redraws.
    return 0;
}
