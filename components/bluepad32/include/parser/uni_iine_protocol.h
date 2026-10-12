// SPDX-License-Identifier: MIT
// Copyright (c) 2026 1ikeMcFlurry
// Adapted from https://github.com/1ikeMcFlurry/esp32c3-iine-gamepad
// See third_party_licenses/iine-gamepad-MIT.txt.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
    uint32_t buttons;
    int32_t axes[6];
    uint8_t axes_present;
    int32_t hat;
    uint8_t dpad;
} iine_input_state_t;

/* KeyLinker transport facts documented by x20ctl; independent bounded C codec.
 * This is not a promise of compatibility with every controller using this UUID. */
#define IINE_PACKET_MAX 20
#define IINE_PAYLOAD_MAX 15
typedef struct {
    uint8_t opcode, serial, length;
    uint8_t payload[IINE_PAYLOAD_MAX];
} iine_packet_t;

size_t iine_packet_build(uint8_t *wire, size_t capacity, uint8_t opcode,
                         uint8_t serial, uint8_t nonce,
                         const uint8_t *payload, size_t length);
bool iine_packet_parse(const uint8_t *wire, size_t length, iine_packet_t *packet);
/* Only our documented, non-persistent discovery/test requests are allowed. */
bool iine_request_allowed(uint8_t opcode, const uint8_t *payload, size_t length);
/* L1241 FW 27.02 measured delta records. Invalid/unknown records never partly
 * update state; the caller retains and displays the raw packet instead. */
bool iine_input_decode(const uint8_t *payload, size_t length, iine_input_state_t *state);
