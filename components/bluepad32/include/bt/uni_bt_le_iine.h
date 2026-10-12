// SPDX-License-Identifier: MIT
#pragma once

#include "uni_hid_device.h"

// All calls run on the BTstack thread, after normal BLE security succeeds.
void uni_bt_le_iine_start(uni_hid_device_t* device);
// Stop every timer/listener before disconnect or device-slot reuse.
void uni_bt_le_iine_reset(uni_hid_device_t* device);
