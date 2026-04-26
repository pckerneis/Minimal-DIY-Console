#pragma once
#include <stdint.h>

// Button indices match spec §3.2 and the input bitmask (§5.2)
#define BTN_LEFT  0
#define BTN_RIGHT 1
#define BTN_UP    2
#define BTN_DOWN  3
#define BTN_A     4
#define BTN_B     5
#define BTN_COUNT 6

void    input_init(void);
void    input_update(void);   // call once per frame before reading state
int     input_btn(int i);     // 1 if button i is held
int     input_btnp(int i);    // 1 if button i was pressed this frame (edge)
uint8_t input_mask(void);     // bitmask of currently held buttons
