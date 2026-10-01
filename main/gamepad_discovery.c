#include "gamepad_discovery.h"

#include <string.h>

void gamepad_copy_name(char *out, size_t capacity, const char *name) {
    if (!out || !capacity) return;
    size_t len = name ? strlen(name) : 0;
    if (len >= capacity) {
        len = capacity - 1;
        while (len && ((unsigned char)name[len] & 0xc0) == 0x80) len--;
    }
    if (len) memcpy(out, name, len);
    out[len] = 0;
}

bool gamepad_discovery_observe(gamepad_discovery_t *state,
                              const uint8_t address[6], uint16_t cod,
                              const char *name) {
    bool keyboard = (cod & 0x1fc0) == 0x0540;
    bool gamepad = cod == 0x0508 && (!name || !name[0] || strstr(name, "Xbox"));
    if (!state || !address || (!keyboard && !gamepad)) return false;

    for (size_t i = 0; i < state->count; i++) {
        gamepad_candidate_t *candidate = &state->candidates[i];
        if (memcmp(candidate->address, address, 6) != 0) continue;
        char shortened[sizeof(candidate->name)];
        gamepad_copy_name(shortened, sizeof(shortened), name);
        if (shortened[0] && strcmp(candidate->name, shortened) != 0) {
            gamepad_copy_name(candidate->name, sizeof(candidate->name), shortened);
            state->revision++;
        }
        return true;
    }
    if (state->count == GAMEPAD_DISCOVERY_MAX) return false;
    gamepad_candidate_t *candidate = &state->candidates[state->count++];
    memcpy(candidate->address, address, 6);
    candidate->keyboard = keyboard;
    if (name) {
        gamepad_copy_name(candidate->name, sizeof(candidate->name), name);
    }
    state->revision++;
    return true;
}

bool gamepad_discovery_select(gamepad_discovery_t *state, size_t index) {
    if (!state || index >= state->count) return false;
    memcpy(state->selected_address, state->candidates[index].address, 6);
    state->selected = true;
    state->passkey_visible = false;
    state->passkey = 0;
    state->pairing_failed = false;
    state->revision++;
    return true;
}

void gamepad_discovery_begin_scan(gamepad_discovery_t *state) {
    if (!state || state->selected || !state->count) return;
    memset(state->candidates, 0, sizeof(state->candidates));
    state->count = 0;
    state->revision++;
}

void gamepad_discovery_clear_selection(gamepad_discovery_t *state) {
    if (!state) return;
    state->selected = false;
    state->passkey_visible = false;
    state->passkey = 0;
    state->pairing_failed = false;
    memset(state->selected_address, 0, sizeof(state->selected_address));
    gamepad_discovery_begin_scan(state);
    state->revision++;
}

bool gamepad_discovery_should_connect(const gamepad_discovery_t *state,
                                      const uint8_t address[6]) {
    return state && address && state->selected &&
           memcmp(state->selected_address, address, 6) == 0;
}

void gamepad_discovery_passkey(gamepad_discovery_t *state, const uint8_t address[6],
                              uint32_t passkey) {
    if (!gamepad_discovery_should_connect(state, address) || passkey > 999999) return;
    state->passkey_visible = true;
    state->passkey = passkey;
    state->pairing_failed = false;
    state->revision++;
}

void gamepad_discovery_pairing_done(gamepad_discovery_t *state, const uint8_t address[6],
                                   bool success) {
    if (!gamepad_discovery_should_connect(state, address)) return;
    state->passkey_visible = false;
    state->passkey = 0;
    state->pairing_failed = !success;
    state->revision++;
}
