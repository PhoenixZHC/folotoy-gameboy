#include "gb_name.h"

#include <stdint.h>
#include <string.h>

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

bool gb_name_valid(const char *name) {
    if (!name) return false;
    size_t length = 0;
    while (length < 32 && name[length]) length++;
    if (!length || length >= 32) return false;
    for (size_t i = 0; i < length;) {
        unsigned char first = (unsigned char)name[i++];
        if (first < 0x80) {
            if (first < 0x20 || first == 0x7f || first == '/' || first == '\\') return false;
            continue;
        }
        uint32_t codepoint;
        unsigned int remaining;
        if (first >= 0xC2 && first <= 0xDF) {
            codepoint = first & 0x1f;
            remaining = 1;
        } else if (first >= 0xE0 && first <= 0xEF) {
            codepoint = first & 0x0f;
            remaining = 2;
        } else if (first >= 0xF0 && first <= 0xF4) {
            codepoint = first & 0x07;
            remaining = 3;
        } else return false;
        if (i + remaining > length) return false;
        for (unsigned int j = 0; j < remaining; j++) {
            unsigned char next = (unsigned char)name[i++];
            if ((next & 0xC0) != 0x80) return false;
            codepoint = (codepoint << 6) | (next & 0x3f);
        }
        if ((remaining == 1 && codepoint < 0x80) ||
            (remaining == 2 && codepoint < 0x800) ||
            (remaining == 3 && codepoint < 0x10000) ||
            (codepoint >= 0xD800 && codepoint <= 0xDFFF) ||
            codepoint > 0xFFFF || codepoint == 0x2028 || codepoint == 0x2029 ||
            codepoint == 0xFEFF ||
            codepoint < 0xA0 || (codepoint >= 0x2000 && codepoint <= 0x200F)) return false;
    }
    return true;
}

bool gb_name_url_decode(const char *encoded, char *output, size_t output_size) {
    if (!encoded || !output || output_size < 2) return false;
    size_t used = 0;
    for (size_t i = 0; encoded[i]; i++) {
        unsigned char value = (unsigned char)encoded[i];
        if (value == '%') {
            int high = hex_digit(encoded[++i]);
            if (high < 0 || !encoded[i] || !encoded[i + 1]) return false;
            int low = hex_digit(encoded[++i]);
            if (low < 0) return false;
            value = (unsigned char)((high << 4) | low);
        } else if (value == '+') value = ' ';
        if (!value || used + 1 >= output_size) return false;
        output[used++] = (char)value;
    }
    output[used] = 0;
    return gb_name_valid(output);
}
