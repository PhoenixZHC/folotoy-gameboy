// SPDX-License-Identifier: MIT
// Copyright (c) 2026 1ikeMcFlurry
// Adapted from https://github.com/1ikeMcFlurry/esp32c3-iine-gamepad
// See third_party_licenses/iine-gamepad-MIT.txt.
#include "parser/uni_iine_protocol.h"
#include <string.h>

static const uint8_t mask[] = {
    161,82,213,163,245,137,246,143,240,157,72,147,234,52,49,186,195,77,198,235,
    73,96,216,163,218,42,83,141,244,97,24,191,174,215,111,81,228,160,217,146
};
static const uint8_t payload_mask[] = {51,99,157,121,242,219,162,26,170,33,139,232,116,211,88};

static uint8_t checksum(const uint8_t *bytes, size_t length)
{
    uint8_t value = 0;
    for (size_t i = 0; i < length; ++i) {
        value ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            value = (uint8_t)((value >> 1) ^ ((value & 1) ? 0xeb : 0));
    }
    return value;
}

size_t iine_packet_build(uint8_t *wire, size_t capacity, uint8_t opcode,
                         uint8_t serial, uint8_t nonce,
                         const uint8_t *payload, size_t length)
{
    if (!wire || length > IINE_PAYLOAD_MAX || capacity < length + 5 || (length && !payload)) return 0;
    uint8_t frame[IINE_PACKET_MAX] = {opcode, (uint8_t)(length + 5), serial, nonce};
    if (length) memcpy(frame + 4, payload, length);
    frame[length + 4] = checksum(frame, length + 4);
    const uint8_t sum = serial + nonce;
    frame[0] ^= (uint8_t)(sum - 154);
    frame[1] ^= (uint8_t)(sum + 155);
    frame[2] = (uint8_t)(serial - 173) ^ nonce;
    frame[3] = (uint8_t)(nonce + 191) ^ 219;
    for (size_t i = 0; i < length; ++i) frame[4 + i] ^= (uint8_t)(sum - payload_mask[i]);
    unsigned offset = (frame[3] & 2) ? 20 : 0;
    for (size_t i = 0; i < length + 5; ++i) wire[i] = frame[i] ^ mask[offset + i];
    return length + 5;
}

bool iine_packet_parse(const uint8_t *wire, size_t length, iine_packet_t *packet)
{
    if (!wire || !packet || length < 5 || length > IINE_PACKET_MAX) return false;
    uint8_t frame[IINE_PACKET_MAX];
    unsigned offset = ((wire[3] ^ mask[3]) & 2) ? 20 : 0;
    for (size_t i = 0; i < length; ++i) frame[i] = wire[i] ^ mask[offset + i];
    frame[3] = (uint8_t)((frame[3] ^ 219) - 191);
    frame[2] = (uint8_t)((frame[2] ^ frame[3]) + 173);
    const uint8_t sum = frame[2] + frame[3];
    frame[0] ^= (uint8_t)(sum - 154);
    frame[1] ^= (uint8_t)(sum + 155);
    for (size_t i = 4; i + 1 < length; ++i) frame[i] ^= (uint8_t)(sum - payload_mask[i - 4]);
    if ((frame[1] & 31) != length || checksum(frame, length - 1) != frame[length - 1]) return false;
    iine_packet_t decoded = {.opcode = frame[0], .serial = frame[2], .length = length - 5};
    if (decoded.length) memcpy(decoded.payload, frame + 4, decoded.length);
    *packet = decoded;
    return true;
}

bool iine_request_allowed(uint8_t opcode, const uint8_t *payload, size_t length)
{
    if (opcode == 0x90 || opcode == 0x91) return length == 0;
    if (!payload) return false;
    if (opcode == 0xb0) return length == 2 && payload[0] == 0 && payload[1] == 1;
    const uint8_t mode[] = {2, 0xdf, 0xab};
    const uint8_t test[] = {5, 0xdf, 0xab, 0, 6, 5};
    if ((opcode == 0x1d || opcode == 0x1e) && length == sizeof(mode))
        return memcmp(payload, mode, sizeof(mode)) == 0;
    return opcode == 0x1d && length == sizeof(test) && !memcmp(payload, test, sizeof(test));
}

bool iine_input_decode(const uint8_t *payload, size_t length, iine_input_state_t *state)
{
    if (!payload || !state || length < 3 || length > IINE_PAYLOAD_MAX || payload[0] != length - 1) return false;
    iine_input_state_t next = *state;
    for (size_t cursor = 1; cursor < length;) {
        if (length - cursor < 2) return false;
        uint8_t active = payload[cursor], code = payload[cursor + 1];
        if (active > 1) return false;
        size_t record = (code == 0x12 || code == 0x13) ? 6 : (code == 7 || code == 8) ? 4 : 2;
        if (length - cursor < record) return false;
        if (code == 0x12 || code == 0x13) {
            unsigned axis = code == 0x12 ? 0 : 3;
            next.axes[axis] = payload[cursor + 2];
            next.axes[axis + 1] = payload[cursor + 3];
            next.axes_present |= 3u << axis;
            /* The following two bytes are the raw stick values, retained in
             * the diagnostic payload. Display uses the processed coordinates. */
        } else if (code >= 0x0d && code <= 0x10) {
            static const uint8_t direction[] = {2, 1, 4, 8}; /* down/up/right/left */
            uint8_t bit = direction[code - 0x0d];
            if (active) next.dpad |= bit; else next.dpad &= (uint8_t)~bit;
        } else {
            unsigned button;
            if (code >= 1 && code <= 8) button = code - 1;
            else if (code == 0x5d) button = 8;
            else if (code == 0x5e) button = 9;
            else if (code == 0x0b || code == 0x0c) button = code - 1;
            else return false;
            uint32_t bit = 1u << button;
            if (active) next.buttons |= bit; else next.buttons &= ~bit;
            if (code == 7 || code == 8) {
                unsigned axis = code == 7 ? 2 : 5;
                next.axes[axis] = payload[cursor + 2];
                next.axes_present |= 1u << axis;
            }
        }
        cursor += record;
    }
    *state = next;
    return true;
}
