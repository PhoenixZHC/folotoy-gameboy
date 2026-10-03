#include "bt/uni_bt_le_advertisement.h"

#include <string.h>

enum { AD_SHORT_NAME = 0x08, AD_COMPLETE_NAME = 0x09, AD_APPEARANCE = 0x19 };

uint16_t uni_bt_le_adv_cod(const uni_bt_le_adv_entry_t* entry) {
    if (!entry) return 0;
    switch (entry->appearance) {
        case 0x03c1: return 0x0540; // Keyboard.
        case 0x03c2: return 0x0580; // Mouse; the platform may reject it.
        case 0x03c3: return 0x0504; // Joystick.
        case 0x03c4: return 0x0508; // Gamepad.
        case 0:
        case 0x03c0: return entry->hid_service ? 0x0500 : 0;
        default: return 0;
    }
}

void uni_bt_le_adv_reset(uni_bt_le_adv_cache_t* cache) {
    memset(cache, 0, sizeof(*cache));
}

const uni_bt_le_adv_entry_t* uni_bt_le_adv_observe(uni_bt_le_adv_cache_t* cache,
                                                const uint8_t address[6], uint8_t address_type,
                                                const uint8_t* data, size_t length, uint32_t now_ms) {
    if (!cache || !address || (!data && length)) return NULL;
    uni_bt_le_adv_entry_t fields = {0};
    bool has_appearance = false;
    // Parse before updating the cache so a truncated packet cannot poison it.
    for (size_t pos = 0; pos < length;) {
        size_t field_length = data[pos++];
        if (!field_length) break;
        if (field_length > length - pos) return NULL;
        uint8_t type = data[pos];
        const uint8_t* value = &data[pos + 1];
        size_t value_length = field_length - 1;
        if (type == 0x02 || type == 0x03) { // Incomplete/complete 16-bit service UUID lists.
            if (value_length % 2) return NULL;
            for (size_t i = 0; i < value_length; i += 2)
                if (value[i] == 0x12 && value[i + 1] == 0x18) fields.hid_service = true;
        } else if (type == 0x06 || type == 0x07) { // Bluetooth base UUID, little endian.
            static const uint8_t hid_uuid[16] = {
                0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
                0x00, 0x10, 0x00, 0x00, 0x12, 0x18, 0x00, 0x00};
            if (value_length % 16) return NULL;
            for (size_t i = 0; i < value_length; i += 16)
                if (memcmp(value + i, hid_uuid, sizeof(hid_uuid)) == 0) fields.hid_service = true;
        } else if (type == AD_APPEARANCE) {
            if (value_length != 2) return NULL;
            fields.appearance = (uint16_t)value[0] | ((uint16_t)value[1] << 8);
            has_appearance = true;
        } else if ((type == AD_COMPLETE_NAME || type == AD_SHORT_NAME) && value_length &&
                   (type == AD_COMPLETE_NAME || !fields.complete_name)) {
            size_t n = value_length;
            if (n >= sizeof(fields.name)) {
                n = sizeof(fields.name) - 1;
                // Avoid cutting a UTF-8 code point at the display boundary.
                while (n && (value[n] & 0xc0) == 0x80) n--;
            }
            memcpy(fields.name, value, n);
            fields.name[n] = 0;
            fields.complete_name = type == AD_COMPLETE_NAME;
        }
        pos += field_length;
    }

    uni_bt_le_adv_entry_t* entry = NULL;
    uni_bt_le_adv_entry_t* spare = NULL;
    uni_bt_le_adv_entry_t* oldest = NULL;
    for (size_t i = 0; i < UNI_BT_LE_ADV_CACHE_SIZE; i++) {
        uni_bt_le_adv_entry_t* item = &cache->entries[i];
        if (item->used && (uint32_t)(now_ms - item->last_seen_ms) >= UNI_BT_LE_ADV_TTL_MS)
            memset(item, 0, sizeof(*item));
        if (!item->used) {
            if (!spare) spare = item;
            continue;
        }
        if (item->address_type == address_type && memcmp(item->address, address, 6) == 0)
            entry = item;
        if (!oldest || (uint32_t)(now_ms - item->last_seen_ms) > (uint32_t)(now_ms - oldest->last_seen_ms))
            oldest = item;
    }
    if (!entry) {
        entry = spare ? spare : oldest;
        memset(entry, 0, sizeof(*entry));
        entry->used = true;
        memcpy(entry->address, address, 6);
        entry->address_type = address_type;
    }
    entry->last_seen_ms = now_ms;
    if (has_appearance) entry->appearance = fields.appearance;
    entry->hid_service |= fields.hid_service;
    // A later shortened name must not overwrite a complete scan-response name.
    if (fields.name[0] && (fields.complete_name || !entry->complete_name)) {
        memcpy(entry->name, fields.name, sizeof(entry->name));
        entry->complete_name = fields.complete_name;
    }
    return entry;
}
