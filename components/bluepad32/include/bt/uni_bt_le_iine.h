// SPDX-License-Identifier: MIT
#pragma once

#include "uni_hid_device.h"

// All calls run on the BTstack thread, after normal BLE security succeeds.
void uni_bt_le_iine_start(uni_hid_device_t* device);
// Stop every timer/listener before disconnect or device-slot reuse.
void uni_bt_le_iine_reset(uni_hid_device_t* device);

// Called on the Bluetooth thread. Unknown firmware waits for explicit approval.
typedef void (*uni_iine_confirmation_cb_t)(uni_hid_device_t* device, bool pending, uint16_t firmware);
void uni_bt_le_iine_set_confirmation_callback(uni_iine_confirmation_cb_t callback);
void uni_bt_le_iine_confirm(uni_hid_device_t* device);
uint16_t uni_bt_le_iine_firmware(const uni_hid_device_t* device);
