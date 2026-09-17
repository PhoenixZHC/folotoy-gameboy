#include "gamepad_discovery.h"

#include <string.h>

bool gamepad_discovery_observe(gamepad_discovery_t *state,
                              const uint8_t address[6], uint16_t cod,
                              const char *name) {
    if (!state || !address || cod != 0x0508 ||
        (name && name[0] && !strstr(name, "Xbox"))) return false;

    for (size_t i = 0; i < state->count; i++) {
        gamepad_candidate_t *candidate = &state->candidates[i];
        if (memcmp(candidate->address, address, 6) != 0) continue;
        if (name && name[0] && strcmp(candidate->name, name) != 0) {
            strncpy(candidate->name, name, sizeof(candidate->name) - 1);
            candidate->name[sizeof(candidate->name) - 1] = 0;
            state->revision++;
        }
        return true;
    }
    if (state->count == GAMEPAD_DISCOVERY_MAX) return false;
    gamepad_candidate_t *candidate = &state->candidates[state->count++];
    memcpy(candidate->address, address, 6);
    if (name) {
        strncpy(candidate->name, name, sizeof(candidate->name) - 1);
        candidate->name[sizeof(candidate->name) - 1] = 0;
    }
    state->revision++;
    return true;
}

bool gamepad_discovery_select(gamepad_discovery_t *state, size_t index) {
    if (!state || index >= state->count) return false;
    memcpy(state->selected_address, state->candidates[index].address, 6);
    state->selected = true;
    state->revision++;
    return true;
}

void gamepad_discovery_clear_selection(gamepad_discovery_t *state) {
    if (!state || !state->selected) return;
    state->selected = false;
    memset(state->selected_address, 0, sizeof(state->selected_address));
    state->revision++;
}

bool gamepad_discovery_should_connect(const gamepad_discovery_t *state,
                                      const uint8_t address[6]) {
    return state && address && state->selected &&
           memcmp(state->selected_address, address, 6) == 0;
}
