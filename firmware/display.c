#include "display.h"
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

// --- Transport config --------------------------------------------------------
// TODO: add SPI transport behind a compile-time flag (DISPLAY_USE_SPI).
//       Replacing transport_init / transport_cmd / transport_flush_fb is
//       all that is needed; the framebuffer and drawing code is transport-agnostic.

#define I2C_PORT  i2c0
#define I2C_SDA   4
#define I2C_SCL   5
#define I2C_FREQ  400000
#define OLED_ADDR 0x3C

static void transport_init(void) {
    i2c_init(I2C_PORT, I2C_FREQ);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);
}

static void transport_cmd(uint8_t cmd) {
    uint8_t buf[2] = {0x00, cmd};
    i2c_write_blocking(I2C_PORT, OLED_ADDR, buf, 2, false);
}

#define PAGES (DISPLAY_H / 8)

static void transport_flush_fb(const uint8_t fb[PAGES][DISPLAY_W]) {
    transport_cmd(0x21); transport_cmd(0);     transport_cmd(127);
    transport_cmd(0x22); transport_cmd(0);     transport_cmd(PAGES - 1);
    uint8_t buf[DISPLAY_W + 1];
    buf[0] = 0x40;
    for (int p = 0; p < PAGES; p++) {
        memcpy(buf + 1, fb[p], DISPLAY_W);
        i2c_write_blocking(I2C_PORT, OLED_ADDR, buf, DISPLAY_W + 1, false);
    }
}

// --- Framebuffer -------------------------------------------------------------

static uint8_t fb[PAGES][DISPLAY_W];

// --- Font --------------------------------------------------------------------
// Monogram by Datagoblin — ASCII 32-126, 5x9px (§3.3)
// Source: third_parties/monogram/bitmap/monogram-bitmap.json, rows [3:12]
// Each entry: 9 bytes, one per row top-to-bottom, bit 0 = leftmost column.

#define FONT_W 5
#define FONT_H 9

static const uint8_t font[][FONT_H] = {
    { 0, 0, 0, 0, 0, 0, 0, 0, 0},  // 32 ' '
    { 4, 4, 4, 4, 4, 0, 4, 0, 0},  // 33 '!'
    {10,10,10, 0, 0, 0, 0, 0, 0},  // 34 '"'
    { 0,10,31,10,10,31,10, 0, 0},  // 35 '#'
    { 4,30, 5,14,20,15, 4, 0, 0},  // 36 '$'
    {17,17, 8, 4, 2,17,17, 0, 0},  // 37 '%'
    { 6, 9, 9,30, 9, 9,22, 0, 0},  // 38 '&'
    { 4, 4, 4, 0, 0, 0, 0, 0, 0},  // 39 '\''
    { 8, 4, 4, 4, 4, 4, 8, 0, 0},  // 40 '('
    { 2, 4, 4, 4, 4, 4, 2, 0, 0},  // 41 ')'
    { 0, 4,21,14,21, 4, 0, 0, 0},  // 42 '*'
    { 0, 4, 4,31, 4, 4, 0, 0, 0},  // 43 '+'
    { 0, 0, 0, 0, 0, 4, 4, 2, 0},  // 44 ','
    { 0, 0, 0,31, 0, 0, 0, 0, 0},  // 45 '-'
    { 0, 0, 0, 0, 0, 4, 4, 0, 0},  // 46 '.'
    {16,16, 8, 4, 2, 1, 1, 0, 0},  // 47 '/'
    {14,17,25,21,19,17,14, 0, 0},  // 48 '0'
    { 4, 6, 4, 4, 4, 4,31, 0, 0},  // 49 '1'
    {14,17,16, 8, 4, 2,31, 0, 0},  // 50 '2'
    {14,17,16,12,16,17,14, 0, 0},  // 51 '3'
    {18,18,17,31,16,16,16, 0, 0},  // 52 '4'
    {31, 1,15,16,16,17,14, 0, 0},  // 53 '5'
    {14, 1, 1,15,17,17,14, 0, 0},  // 54 '6'
    {31,16,16, 8, 4, 4, 4, 0, 0},  // 55 '7'
    {14,17,17,14,17,17,14, 0, 0},  // 56 '8'
    {14,17,17,30,16,17,14, 0, 0},  // 57 '9'
    { 0, 4, 4, 0, 0, 4, 4, 0, 0},  // 58 ':'
    { 0, 4, 4, 0, 0, 4, 4, 2, 0},  // 59 ';'
    { 0,24, 6, 1, 6,24, 0, 0, 0},  // 60 '<'
    { 0, 0,31, 0,31, 0, 0, 0, 0},  // 61 '='
    { 0, 3,12,16,12, 3, 0, 0, 0},  // 62 '>'
    {14,17,16, 8, 4, 0, 4, 0, 0},  // 63 '?'
    {14,25,21,21,25, 1,14, 0, 0},  // 64 '@'
    {14,17,17,17,31,17,17, 0, 0},  // 65 'A'
    {15,17,17,15,17,17,15, 0, 0},  // 66 'B'
    {14,17, 1, 1, 1,17,14, 0, 0},  // 67 'C'
    {15,17,17,17,17,17,15, 0, 0},  // 68 'D'
    {31, 1, 1,15, 1, 1,31, 0, 0},  // 69 'E'
    {31, 1, 1,15, 1, 1, 1, 0, 0},  // 70 'F'
    {14,17, 1,29,17,17,14, 0, 0},  // 71 'G'
    {17,17,17,31,17,17,17, 0, 0},  // 72 'H'
    {31, 4, 4, 4, 4, 4,31, 0, 0},  // 73 'I'
    {16,16,16,16,17,17,14, 0, 0},  // 74 'J'
    {17, 9, 5, 3, 5, 9,17, 0, 0},  // 75 'K'
    { 1, 1, 1, 1, 1, 1,31, 0, 0},  // 76 'L'
    {17,27,21,17,17,17,17, 0, 0},  // 77 'M'
    {17,17,19,21,25,17,17, 0, 0},  // 78 'N'
    {14,17,17,17,17,17,14, 0, 0},  // 79 'O'
    {15,17,17,15, 1, 1, 1, 0, 0},  // 80 'P'
    {14,17,17,17,17,17,14,24, 0},  // 81 'Q'
    {15,17,17,15,17,17,17, 0, 0},  // 82 'R'
    {14,17, 1,14,16,17,14, 0, 0},  // 83 'S'
    {31, 4, 4, 4, 4, 4, 4, 0, 0},  // 84 'T'
    {17,17,17,17,17,17,14, 0, 0},  // 85 'U'
    {17,17,17,17,10,10, 4, 0, 0},  // 86 'V'
    {17,17,17,17,21,27,17, 0, 0},  // 87 'W'
    {17,17,10, 4,10,17,17, 0, 0},  // 88 'X'
    {17,17,10, 4, 4, 4, 4, 0, 0},  // 89 'Y'
    {31,16, 8, 4, 2, 1,31, 0, 0},  // 90 'Z'
    {12, 4, 4, 4, 4, 4,12, 0, 0},  // 91 '['
    { 1, 1, 2, 4, 8,16,16, 0, 0},  // 92 '\\'
    { 6, 4, 4, 4, 4, 4, 6, 0, 0},  // 93 ']'
    { 4,10,17, 0, 0, 0, 0, 0, 0},  // 94 '^'
    { 0, 0, 0, 0, 0, 0,31, 0, 0},  // 95 '_'
    { 2, 4, 0, 0, 0, 0, 0, 0, 0},  // 96 '`'
    { 0, 0,30,17,17,17,30, 0, 0},  // 97 'a'
    { 1, 1,15,17,17,17,15, 0, 0},  // 98 'b'
    { 0, 0,14,17, 1,17,14, 0, 0},  // 99 'c'
    {16,16,30,17,17,17,30, 0, 0},  // 100 'd'
    { 0, 0,14,17,31, 1,14, 0, 0},  // 101 'e'
    {12,18, 2,15, 2, 2, 2, 0, 0},  // 102 'f'
    { 0, 0,30,17,17,17,30,16,14},  // 103 'g'
    { 1, 1,15,17,17,17,17, 0, 0},  // 104 'h'
    { 4, 0, 6, 4, 4, 4,31, 0, 0},  // 105 'i'
    {16, 0,24,16,16,16,16,17,14},  // 106 'j'
    { 1, 1,17, 9, 7, 9,17, 0, 0},  // 107 'k'
    { 3, 2, 2, 2, 2, 2,28, 0, 0},  // 108 'l'
    { 0, 0,15,21,21,21,21, 0, 0},  // 109 'm'
    { 0, 0,15,17,17,17,17, 0, 0},  // 110 'n'
    { 0, 0,14,17,17,17,14, 0, 0},  // 111 'o'
    { 0, 0,15,17,17,17,15, 1, 1},  // 112 'p'
    { 0, 0,30,17,17,17,30,16,16},  // 113 'q'
    { 0, 0,13,19, 1, 1, 1, 0, 0},  // 114 'r'
    { 0, 0,30, 1,14,16,15, 0, 0},  // 115 's'
    { 2, 2,15, 2, 2, 2,28, 0, 0},  // 116 't'
    { 0, 0,17,17,17,17,30, 0, 0},  // 117 'u'
    { 0, 0,17,17,17,10, 4, 0, 0},  // 118 'v'
    { 0, 0,17,17,21,21,10, 0, 0},  // 119 'w'
    { 0, 0,17,10, 4,10,17, 0, 0},  // 120 'x'
    { 0, 0,17,17,17,17,30,16,14},  // 121 'y'
    { 0, 0,31, 8, 4, 2,31, 0, 0},  // 122 'z'
    { 8, 4, 4, 2, 4, 4, 8, 0, 0},  // 123 '{'
    { 4, 4, 4, 4, 4, 4, 4, 0, 0},  // 124 '|'
    { 2, 4, 4, 8, 4, 4, 2, 0, 0},  // 125 '}'
    { 0, 0,18,13, 0, 0, 0, 0, 0},  // 126 '~'
};

// --- Drawing helpers ---------------------------------------------------------

static void fb_pixel(int x, int y, int c) {
    if ((unsigned)x >= DISPLAY_W || (unsigned)y >= DISPLAY_H) return;
    if (c) fb[y / 8][x] |=  (1u << (y % 8));
    else   fb[y / 8][x] &= ~(1u << (y % 8));
}

static int draw_char(int x, int y, char ch) {
    if (ch < 32 || ch > 126) ch = '?';
    const uint8_t *g = font[(unsigned char)ch - 32];
    for (int row = 0; row < FONT_H; row++) {
        uint8_t bits = g[row];
        for (int col = 0; col < FONT_W; col++)
            fb_pixel(x + col, y + row, (bits >> col) & 1);
    }
    return x + FONT_W + 1;
}

// --- Public API --------------------------------------------------------------

void display_init(void) {
    transport_init();
    sleep_ms(100);
    transport_cmd(0xAE);
    transport_cmd(0xD5); transport_cmd(0x80);
    transport_cmd(0xA8); transport_cmd(0x3F);
    transport_cmd(0xD3); transport_cmd(0x00);
    transport_cmd(0x40);
    transport_cmd(0x8D); transport_cmd(0x14);
    transport_cmd(0x20); transport_cmd(0x00);
    transport_cmd(0xA1);
    transport_cmd(0xC8);
    transport_cmd(0xDA); transport_cmd(0x12);
    transport_cmd(0x81); transport_cmd(0xCF);
    transport_cmd(0xD9); transport_cmd(0xF1);
    transport_cmd(0xDB); transport_cmd(0x40);
    transport_cmd(0xA4);
    transport_cmd(0xA6);
    transport_cmd(0xAF);
}

void display_flush(void) {
    transport_flush_fb(fb);
}

void display_cls(int c) {
    memset(fb, c ? 0xFF : 0x00, sizeof(fb));
}

void display_pset(int x, int y, int c) {
    fb_pixel(x, y, c);
}

void display_rectfill(int x, int y, int w, int h, int c) {
    for (int py = y; py < y + h; py++)
        for (int px = x; px < x + w; px++)
            fb_pixel(px, py, c);
}

void display_line(int x0, int y0, int x1, int y1, int c) {
    int dx =  abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        fb_pixel(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void display_print(int x, int y, const char *s) {
    while (*s && x + 5 < DISPLAY_W)
        x = draw_char(x, y, *s++);
}
