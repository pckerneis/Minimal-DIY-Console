#pragma once
#include <stdbool.h>
#include <stdint.h>

// Load a compiled cart binary. Returns false if the binary is invalid.
bool vm_load(const uint8_t *bin, uint32_t len);

// Lifecycle hooks — no-ops until vm_load succeeds
void vm_call_init(void);
void vm_call_update(int frame, uint8_t input);
void vm_call_draw(int frame, uint8_t input);
int  vm_call_audio(int t); // returns sample [0, 255]
