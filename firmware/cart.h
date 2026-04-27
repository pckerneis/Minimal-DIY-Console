#pragma once
#include <stdint.h>

// Number of valid .bdb carts in flash storage.
int cart_count(void);

// XIP pointer + size for the cart at index. Returns NULL if out of range.
const uint8_t *cart_get(int index, uint32_t *out_size);

// Value of a named metadata field from the cart at index, or "" if not found.
// Result is in a static buffer valid until the next call.
const char *cart_meta(int index, const char *field);
