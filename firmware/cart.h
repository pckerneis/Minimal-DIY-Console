#pragma once
#include <stdint.h>

// Number of valid .bdb carts in flash storage.
int cart_count(void);

// XIP pointer + size for the cart at index. Returns NULL if out of range.
const uint8_t *cart_get(int index, uint32_t *out_size);

// Value of a named metadata field from the cart at index, or "" if not found.
// Result is in a static buffer valid until the next call.
const char *cart_meta(int index, const char *field);

// XIP pointer + size for the cart with the given 8.3 filename (case-insensitive),
// or NULL if not found or invalid.
const uint8_t *cart_find_by_name(const char *name, uint32_t *out_size);
