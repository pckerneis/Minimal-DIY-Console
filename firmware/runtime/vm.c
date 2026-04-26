#include "vm.h"

bool vm_load(const uint8_t *bin, uint32_t len) {
    (void)bin; (void)len;
    return false; // TODO: implement
}

void vm_call_init(void)                         {}
void vm_call_update(int frame, uint8_t input)   { (void)frame; (void)input; }
void vm_call_draw(int frame, uint8_t input)     { (void)frame; (void)input; }
int  vm_call_audio(int t)                       { (void)t; return 128; }
