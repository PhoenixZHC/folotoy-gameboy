#include <assert.h>
#include <string.h>

#include "gamepad_discovery.h"

int main(void) {
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
    assert(!gamepad_discovery_select(&state, 2));
    return 0;
}
