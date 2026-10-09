// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stdint.h>
struct uni_hid_device_s;
// Standard BLE gamepads: bounded per-report state, using the peer's report map.
bool uni_hid_parser_standard_configure(struct uni_hid_device_s* d);
void uni_hid_parser_standard_setup(struct uni_hid_device_s* d);
void uni_hid_parser_standard_parse(struct uni_hid_device_s* d, const uint8_t* report, uint16_t len);
