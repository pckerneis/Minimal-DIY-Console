#include "input.h"
#include "pico/stdlib.h"

// GP6=Left GP7=Right GP8=Up GP9=Down GP10=A GP11=B (active-low, internal pull-up)
static const uint BTN_PINS[BTN_COUNT] = {6, 7, 8, 9, 10, 11};

static bool cur[BTN_COUNT];
static bool pressed[BTN_COUNT]; // true for one frame on the rising edge

void input_init(void) {
    for (int i = 0; i < BTN_COUNT; i++) {
        gpio_init(BTN_PINS[i]);
        gpio_set_dir(BTN_PINS[i], GPIO_IN);
        gpio_pull_up(BTN_PINS[i]);
        cur[i] = false;
    }
}

void input_update(void) {
    for (int i = 0; i < BTN_COUNT; i++) {
        bool raw = !gpio_get(BTN_PINS[i]); // active-low → invert
        pressed[i] = raw && !cur[i];
        cur[i]     = raw;
    }
}

int input_btn(int i) {
    if (i < 0 || i >= BTN_COUNT) return 0;
    return cur[i] ? 1 : 0;
}

int input_btnp(int i) {
    if (i < 0 || i >= BTN_COUNT) return 0;
    return pressed[i] ? 1 : 0;
}

uint8_t input_mask(void) {
    uint8_t mask = 0;
    for (int i = 0; i < BTN_COUNT; i++)
        if (cur[i]) mask |= (1u << i);
    return mask;
}
