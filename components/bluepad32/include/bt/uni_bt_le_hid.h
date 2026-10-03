#ifndef UNI_BT_LE_HID_H
#define UNI_BT_LE_HID_H

#include <stddef.h>
#include <stdint.h>

// Classify top-level Application collections with data Input items.
// Returns keyboard/gamepad/joystick COD, or zero for unsupported/ambiguous maps.
// This admission check does not replace the HID report parser.
uint16_t uni_bt_le_hid_cod(const uint8_t* descriptor, size_t length);

#endif
