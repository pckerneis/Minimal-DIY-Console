#pragma once

#define DISPLAY_W 128
#define DISPLAY_H 64

void display_init(void);
void display_flush(void);

// Cart API primitives (§3.3)
void display_cls(int c);
void display_pset(int x, int y, int c);
void display_rectfill(int x, int y, int w, int h, int c);
void display_line(int x0, int y0, int x1, int y1, int c);
void display_print(int x, int y, const char *s);
