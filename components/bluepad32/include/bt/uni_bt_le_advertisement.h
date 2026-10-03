#ifndef UNI_BT_LE_ADVERTISEMENT_H
#define UNI_BT_LE_ADVERTISEMENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UNI_BT_LE_ADV_CACHE_SIZE 16
#define UNI_BT_LE_ADV_TTL_MS 5000u
#define UNI_BT_LE_ADV_NAME_SIZE 64

typedef struct {
    bool used;
    bool complete_name;
    bool hid_service;
    uint8_t address[6];
    uint8_t address_type;
    uint16_t appearance;
    uint32_t last_seen_ms;
    char name[UNI_BT_LE_ADV_NAME_SIZE];
} uni_bt_le_adv_entry_t;

typedef struct {
    uni_bt_le_adv_entry_t entries[UNI_BT_LE_ADV_CACHE_SIZE];
} uni_bt_le_adv_cache_t;

// Called only on the Bluetooth thread. Returns NULL for malformed AD data.
// Entries expire after inactivity; a full cache replaces the least recent peer.
// The returned entry belongs to cache and is valid until the next update/reset.
const uni_bt_le_adv_entry_t* uni_bt_le_adv_observe(uni_bt_le_adv_cache_t* cache,
                                                const uint8_t address[6], uint8_t address_type,
                                                const uint8_t* data, size_t length, uint32_t now_ms);
void uni_bt_le_adv_reset(uni_bt_le_adv_cache_t* cache);
// 0x0500 is a provisional HID candidate, not an identified gamepad.
uint16_t uni_bt_le_adv_cod(const uni_bt_le_adv_entry_t* entry);

#endif
