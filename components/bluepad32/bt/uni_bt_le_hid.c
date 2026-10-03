#include "bt/uni_bt_le_hid.h"

#include <stdbool.h>

uint16_t uni_bt_le_hid_cod(const uint8_t* descriptor, size_t length) {
    if (!descriptor || !length) return 0;
    uint32_t page = 0, pages[8], usage = 0;
    unsigned saved = 0, depth = 0;
    uint16_t application = 0, result = 0;
    bool have_usage = false;
    for (size_t pos = 0; pos < length;) {
        uint8_t prefix = descriptor[pos++];
        // Long/reserved items cannot establish a supported collection here.
        if (prefix == 0xfe) return 0;
        unsigned size = prefix & 3;
        if (size == 3) size = 4;
        if (size > length - pos) return 0;
        uint32_t value = 0;
        for (unsigned i = 0; i < size; i++) value |= (uint32_t)descriptor[pos++] << (8 * i);
        unsigned type = (prefix >> 2) & 3, tag = prefix >> 4;
        if (type == 1) { // Global.
            if (tag == 0) page = value;
            else if (tag == 10) {
                if (size || saved == 8) return 0;
                pages[saved++] = page;
            } else if (tag == 11) {
                if (size || !saved) return 0;
                page = pages[--saved];
            }
        } else if (type == 2) { // Local; retain the first usage of a collection.
            if ((tag == 0 || tag == 1) && !have_usage) {
                usage = size == 4 ? value : (page << 16) | value;
                have_usage = true;
            }
            if (tag == 10) return 0; // Delimited usage alternatives are ambiguous.
        } else if (type == 0) { // Local state expires after every Main item.
            if (tag == 10) { // Collection.
                if (size != 1 || depth == 32) return 0;
                if (!depth) {
                    application = 0;
                    if (value == 1 && have_usage) {
                        if (usage == 0x10004) application = 0x0504;
                        else if (usage == 0x10005) application = 0x0508;
                        else if (usage == 0x10006) application = 0x0540;
                        else if ((usage >> 16) == 1) application = 0xffff;
                    }
                }
                depth++;
            } else if (tag == 12) { // End Collection.
                if (size || !depth) return 0;
                if (!--depth) application = 0;
            } else if (tag == 8) { // Input; constants do not identify an input device.
                if (!depth || !size) return 0;
                if (!(value & 1) && application) {
                    if (application == 0xffff || (result && result != application)) return 0;
                    result = application;
                }
            }
            have_usage = false;
        } else return 0;
    }
    return depth || saved ? 0 : result;
}
