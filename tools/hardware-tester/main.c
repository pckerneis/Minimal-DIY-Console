#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

#if defined(CYW43_WL_GPIO_LED_PIN)
#include "pico/cyw43_arch.h"
#define LED_INIT()  cyw43_arch_init()
#define LED_ON()    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1)
#define LED_OFF()   cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0)
#else
#define LED_INIT()  do { gpio_init(PICO_DEFAULT_LED_PIN); gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT); } while (0)
#define LED_ON()    gpio_put(PICO_DEFAULT_LED_PIN, 1)
#define LED_OFF()   gpio_put(PICO_DEFAULT_LED_PIN, 0)
#endif

// --- Hardware config ---------------------------------------------------------
#define I2C_PORT    i2c0
#define I2C_SDA     4
#define I2C_SCL     5
#define I2C_FREQ    400000

// --- SSD1306 config ----------------------------------------------------------
#define OLED_ADDR   0x3C
#define OLED_W      128
#define OLED_H      64
#define OLED_PAGES  (OLED_H / 8)

static uint8_t fb[OLED_PAGES][OLED_W];

// --- Minimal 5x7 ASCII font (chars 32–126) -----------------------------------
// Each entry: 5 bytes, each byte = one column, bit0 = top row
static const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // 32 space
    {0x00,0x00,0x5F,0x00,0x00}, // 33 !
    {0x00,0x07,0x00,0x07,0x00}, // 34 "
    {0x14,0x7F,0x14,0x7F,0x14}, // 35 #
    {0x24,0x2A,0x7F,0x2A,0x12}, // 36 $
    {0x23,0x13,0x08,0x64,0x62}, // 37 %
    {0x36,0x49,0x55,0x22,0x50}, // 38 &
    {0x00,0x05,0x03,0x00,0x00}, // 39 '
    {0x00,0x1C,0x22,0x41,0x00}, // 40 (
    {0x00,0x41,0x22,0x1C,0x00}, // 41 )
    {0x14,0x08,0x3E,0x08,0x14}, // 42 *
    {0x08,0x08,0x3E,0x08,0x08}, // 43 +
    {0x00,0x50,0x30,0x00,0x00}, // 44 ,
    {0x08,0x08,0x08,0x08,0x08}, // 45 -
    {0x00,0x60,0x60,0x00,0x00}, // 46 .
    {0x20,0x10,0x08,0x04,0x02}, // 47 /
    {0x3E,0x51,0x49,0x45,0x3E}, // 48 0
    {0x00,0x42,0x7F,0x40,0x00}, // 49 1
    {0x42,0x61,0x51,0x49,0x46}, // 50 2
    {0x21,0x41,0x45,0x4B,0x31}, // 51 3
    {0x18,0x14,0x12,0x7F,0x10}, // 52 4
    {0x27,0x45,0x45,0x45,0x39}, // 53 5
    {0x3C,0x4A,0x49,0x49,0x30}, // 54 6
    {0x01,0x71,0x09,0x05,0x03}, // 55 7
    {0x36,0x49,0x49,0x49,0x36}, // 56 8
    {0x06,0x49,0x49,0x29,0x1E}, // 57 9
    {0x00,0x36,0x36,0x00,0x00}, // 58 :
    {0x00,0x56,0x36,0x00,0x00}, // 59 ;
    {0x08,0x14,0x22,0x41,0x00}, // 60 <
    {0x14,0x14,0x14,0x14,0x14}, // 61 =
    {0x00,0x41,0x22,0x14,0x08}, // 62 >
    {0x02,0x01,0x51,0x09,0x06}, // 63 ?
    {0x32,0x49,0x79,0x41,0x3E}, // 64 @
    {0x7E,0x11,0x11,0x11,0x7E}, // 65 A
    {0x7F,0x49,0x49,0x49,0x36}, // 66 B
    {0x3E,0x41,0x41,0x41,0x22}, // 67 C
    {0x7F,0x41,0x41,0x22,0x1C}, // 68 D
    {0x7F,0x49,0x49,0x49,0x41}, // 69 E
    {0x7F,0x09,0x09,0x09,0x01}, // 70 F
    {0x3E,0x41,0x49,0x49,0x7A}, // 71 G
    {0x7F,0x08,0x08,0x08,0x7F}, // 72 H
    {0x00,0x41,0x7F,0x41,0x00}, // 73 I
    {0x20,0x40,0x41,0x3F,0x01}, // 74 J
    {0x7F,0x08,0x14,0x22,0x41}, // 75 K
    {0x7F,0x40,0x40,0x40,0x40}, // 76 L
    {0x7F,0x02,0x04,0x02,0x7F}, // 77 M
    {0x7F,0x04,0x08,0x10,0x7F}, // 78 N
    {0x3E,0x41,0x41,0x41,0x3E}, // 79 O
    {0x7F,0x09,0x09,0x09,0x06}, // 80 P
    {0x3E,0x41,0x51,0x21,0x5E}, // 81 Q
    {0x7F,0x09,0x19,0x29,0x46}, // 82 R
    {0x46,0x49,0x49,0x49,0x31}, // 83 S
    {0x01,0x01,0x7F,0x01,0x01}, // 84 T
    {0x3F,0x40,0x40,0x40,0x3F}, // 85 U
    {0x1F,0x20,0x40,0x20,0x1F}, // 86 V
    {0x3F,0x40,0x38,0x40,0x3F}, // 87 W
    {0x63,0x14,0x08,0x14,0x63}, // 88 X
    {0x07,0x08,0x70,0x08,0x07}, // 89 Y
    {0x61,0x51,0x49,0x45,0x43}, // 90 Z
    {0x00,0x7F,0x41,0x41,0x00}, // 91 [
    {0x02,0x04,0x08,0x10,0x20}, // 92 backslash
    {0x00,0x41,0x41,0x7F,0x00}, // 93 ]
    {0x04,0x02,0x01,0x02,0x04}, // 94 ^
    {0x40,0x40,0x40,0x40,0x40}, // 95 _
    {0x00,0x01,0x02,0x04,0x00}, // 96 `
    {0x20,0x54,0x54,0x54,0x78}, // 97 a
    {0x7F,0x48,0x44,0x44,0x38}, // 98 b
    {0x38,0x44,0x44,0x44,0x20}, // 99 c
    {0x38,0x44,0x44,0x48,0x7F}, // 100 d
    {0x38,0x54,0x54,0x54,0x18}, // 101 e
    {0x08,0x7E,0x09,0x01,0x02}, // 102 f
    {0x0C,0x52,0x52,0x52,0x3E}, // 103 g
    {0x7F,0x08,0x04,0x04,0x78}, // 104 h
    {0x00,0x44,0x7D,0x40,0x00}, // 105 i
    {0x20,0x40,0x44,0x3D,0x00}, // 106 j
    {0x7F,0x10,0x28,0x44,0x00}, // 107 k
    {0x00,0x41,0x7F,0x40,0x00}, // 108 l
    {0x7C,0x04,0x18,0x04,0x78}, // 109 m
    {0x7C,0x08,0x04,0x04,0x78}, // 110 n
    {0x38,0x44,0x44,0x44,0x38}, // 111 o
    {0x7C,0x14,0x14,0x14,0x08}, // 112 p
    {0x08,0x14,0x14,0x18,0x7C}, // 113 q
    {0x7C,0x08,0x04,0x04,0x08}, // 114 r
    {0x48,0x54,0x54,0x54,0x20}, // 115 s
    {0x04,0x3F,0x44,0x40,0x20}, // 116 t
    {0x3C,0x40,0x40,0x20,0x7C}, // 117 u
    {0x1C,0x20,0x40,0x20,0x1C}, // 118 v
    {0x3C,0x40,0x30,0x40,0x3C}, // 119 w
    {0x44,0x28,0x10,0x28,0x44}, // 120 x
    {0x0C,0x50,0x50,0x50,0x3C}, // 121 y
    {0x44,0x64,0x54,0x4C,0x44}, // 122 z
    {0x00,0x08,0x36,0x41,0x00}, // 123 {
    {0x00,0x00,0x7F,0x00,0x00}, // 124 |
    {0x00,0x41,0x36,0x08,0x00}, // 125 }
    {0x10,0x08,0x08,0x10,0x08}, // 126 ~
};

// --- SSD1306 driver ----------------------------------------------------------

static void oled_write_cmd(uint8_t cmd) {
    uint8_t buf[2] = {0x00, cmd};
    i2c_write_blocking(I2C_PORT, OLED_ADDR, buf, 2, false);
}

static void oled_init(void) {
    sleep_ms(100);
    oled_write_cmd(0xAE);        // display off
    oled_write_cmd(0xD5); oled_write_cmd(0x80); // clock divide
    oled_write_cmd(0xA8); oled_write_cmd(0x3F); // mux ratio 63
    oled_write_cmd(0xD3); oled_write_cmd(0x00); // display offset 0
    oled_write_cmd(0x40);        // start line 0
    oled_write_cmd(0x8D); oled_write_cmd(0x14); // charge pump on
    oled_write_cmd(0x20); oled_write_cmd(0x00); // horizontal addressing
    oled_write_cmd(0xA1);        // seg remap
    oled_write_cmd(0xC8);        // COM scan direction
    oled_write_cmd(0xDA); oled_write_cmd(0x12); // COM pins
    oled_write_cmd(0x81); oled_write_cmd(0xCF); // contrast
    oled_write_cmd(0xD9); oled_write_cmd(0xF1); // pre-charge
    oled_write_cmd(0xDB); oled_write_cmd(0x40); // VCOMH
    oled_write_cmd(0xA4);        // resume from RAM
    oled_write_cmd(0xA6);        // normal (not inverted)
    oled_write_cmd(0xAF);        // display on
}

static void oled_flush(void) {
    oled_write_cmd(0x21); oled_write_cmd(0); oled_write_cmd(127);
    oled_write_cmd(0x22); oled_write_cmd(0); oled_write_cmd(OLED_PAGES - 1);

    uint8_t buf[OLED_W + 1];
    buf[0] = 0x40;
    for (int p = 0; p < OLED_PAGES; p++) {
        memcpy(buf + 1, fb[p], OLED_W);
        i2c_write_blocking(I2C_PORT, OLED_ADDR, buf, OLED_W + 1, false);
    }
}

static void oled_clear(void) {
    memset(fb, 0, sizeof(fb));
}

static void oled_pixel(int x, int y, bool on) {
    if ((unsigned)x >= OLED_W || (unsigned)y >= OLED_H) return;
    if (on) fb[y / 8][x] |=  (1u << (y % 8));
    else    fb[y / 8][x] &= ~(1u << (y % 8));
}

static void oled_hline(int x0, int x1, int y) {
    for (int x = x0; x <= x1; x++) oled_pixel(x, y, true);
}

static void oled_vline(int x, int y0, int y1) {
    for (int y = y0; y <= y1; y++) oled_pixel(x, y, true);
}

static void oled_rect(int x, int y, int w, int h) {
    oled_hline(x, x + w - 1, y);
    oled_hline(x, x + w - 1, y + h - 1);
    oled_vline(x, y, y + h - 1);
    oled_vline(x + w - 1, y, y + h - 1);
}

static int oled_char(int x, int y, char c) {
    if (c < 32 || c > 126) c = '?';
    const uint8_t *g = font5x7[(unsigned char)c - 32];
    for (int col = 0; col < 5; col++) {
        uint8_t bits = g[col];
        for (int row = 0; row < 7; row++)
            oled_pixel(x + col, y + row, (bits >> row) & 1);
    }
    return x + 6;
}

static void oled_string(int x, int y, const char *s) {
    while (*s && x + 5 < OLED_W)
        x = oled_char(x, y, *s++);
}

// --- Button config -----------------------------------------------------------
#define BTN_COUNT 6
static const uint BTN_PINS[BTN_COUNT] = {6, 7, 8, 9, 10, 11};
static const char *BTN_NAMES[BTN_COUNT] = {"L", "R", "U", "D", "A", "B"};
#define DEBOUNCE_MS 50

// --- Button state ------------------------------------------------------------
static bool     btn_prev[BTN_COUNT];
static uint32_t btn_last_ms[BTN_COUNT];

static void buttons_init(void) {
    for (int i = 0; i < BTN_COUNT; i++) {
        gpio_init(BTN_PINS[i]);
        gpio_set_dir(BTN_PINS[i], GPIO_IN);
        gpio_pull_up(BTN_PINS[i]);
        btn_prev[i] = true; // pulled high = not pressed
    }
}

// Returns bitmask of newly-pressed buttons this call
static uint8_t buttons_read(void) {
    uint8_t pressed = 0;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    for (int i = 0; i < BTN_COUNT; i++) {
        bool cur = gpio_get(BTN_PINS[i]);
        if (!cur && btn_prev[i] && (now - btn_last_ms[i]) > DEBOUNCE_MS) {
            pressed |= (1u << i);
            btn_last_ms[i] = now;
        }
        btn_prev[i] = cur;
    }
    return pressed;
}

// --- Draw button status row --------------------------------------------------
static void draw_buttons(uint8_t held_mask) {
    // 6 boxes across the bottom two rows
    for (int i = 0; i < BTN_COUNT; i++) {
        int x = 2 + i * 21;
        int y = 48;
        bool held = (held_mask >> i) & 1;
        oled_rect(x, y, 19, 14);
        if (held) {
            // filled when pressed
            for (int px = x + 1; px < x + 18; px++)
                oled_vline(px, y + 1, y + 12);
        }
        // label centred in box (5px wide char, box 19px wide → offset 7)
        oled_char(x + 7, y + 4, BTN_NAMES[i][0]);
    }
}

// --- Main test ---------------------------------------------------------------

int main(void) {
    stdio_init_all();
    LED_INIT();
    LED_ON();

    i2c_init(I2C_PORT, I2C_FREQ);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    buttons_init();

    oled_init();

    // --- Splash screen -------------------------------------------------------
    oled_clear();
    oled_rect(0, 0, OLED_W, OLED_H);
    oled_string(4,  4, "Whadda WPI438");
    oled_hline(4, 123, 13);
    oled_string(4, 18, "OLED + Button test");
    oled_string(4, 30, "Buttons: GP6-GP11");
    oled_string(4, 42, "Wire to GND");
    oled_flush();
    sleep_ms(2500);

    // --- Button test loop ----------------------------------------------------
    printf("Button test ready. Wire each button between its GP pin and GND.\n");
    printf("  A=GP6  B=GP7  C=GP8  D=GP9  E=GP10  F=GP11\n\n");

    char last_event[22] = "Press a button...   ";
    uint8_t held_mask = 0;

    while (true) {
        uint8_t pressed = buttons_read();

        // Update held mask (simple: show buttons currently low)
        held_mask = 0;
        for (int i = 0; i < BTN_COUNT; i++)
            if (!gpio_get(BTN_PINS[i]))
                held_mask |= (1u << i);

        if (pressed) {
            for (int i = 0; i < BTN_COUNT; i++) {
                if ((pressed >> i) & 1) {
                    printf("Button %s pressed (GP%d)\n", BTN_NAMES[i], BTN_PINS[i]);
                    snprintf(last_event, sizeof(last_event),
                             "Btn %s  GP%d pressed", BTN_NAMES[i], BTN_PINS[i]);
                    LED_ON();
                }
            }
        } else {
            LED_OFF();
        }

        oled_clear();
        oled_rect(0, 0, OLED_W, OLED_H);
        oled_string(4, 4, "Button test");
        oled_hline(4, 123, 13);
        oled_string(4, 17, last_event);
        oled_hline(4, 123, 27);
        draw_buttons(held_mask);
        oled_flush();

        sleep_ms(20);
    }
}
