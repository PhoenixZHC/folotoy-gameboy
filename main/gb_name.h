#pragma once

#include <stdbool.h>
#include <stddef.h>

// ROM directory names are UTF-8, null-terminated, and at most 31 bytes.
bool gb_name_valid(const char *name);
bool gb_name_url_decode(const char *encoded, char *output, size_t output_size);
