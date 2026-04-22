# Minimal-DIY-Console (working name)

Minimal DIY Console is a tiny, open, DIY platform designed to run on minimal hardware such as a Raspberry Pi Pico.

It is built around:
- simple graphics (monochrome 128x64 screen)
- 6-button input
- procedural sound
- extremely small programs

The philosophy is to serve as a creative and educational platform for creating apps, arts and game end to end, from hardware to software.

## Example cart

```
init() {
  // ball
  bx = 64
  by = 32
  bvx = 1
  bvy = 1

  py = 28 // player paddle Y position
  ey = 28 // enemy paddle Y position

  hit = 0
}

update(frame, input) {
  // move paddle
  if (btn(2)) py -= 2
  if (btn(3)) py += 2
  if (py < 0) py = 0
  if (py > 54) py = 54

  // move ball
  bx += bvx
  by += bvy

  // wall bounce
  if (by < 0 || by > 63) {
    bvy = -bvy
  }

  // collision with player paddle
  if (bx < 6 && by >= py && by <= py + 10) {
    bvx = 1
  }

  // enemy AI
  if (by > ey + 5) ey += 1
  if (by < ey + 5) ey -= 1

  // collision with enemy paddle
  if (bx > 122 && ey >= py && by <= ey + 10) {
    bvx = -1
  }

  // reset when out of bounds
  if (bx < 0 || bx > 127) {
    bx = 64
    by = 32
    bvx = -bvx
  }
}

draw() {
  cls(0)

  rectfill(2, py, 4, 10, 1) // player
  rectfill(122, ey, 4, 10, 1) // enemy
  rectfill(bx, by, 2, 2, 1) // ball
}
```

## Program structure

A cart defines up to four functions:
- `init()` runs once at start
- `update(frame, input)` 30 times per second
- `draw(frame, input)` 30 times per second
- `audio(t)` 22050 times per second

## API & Scripting

### Expressions

Supports integer-only math with 32 bit wraparound. Supported operators:

```
+ - * / %
> < >= <= ==
& | ^ >> <<
```

### Variables

Variables are globally accessible and created on first assignment with `=`.

Shortcuts available: `-=`, `+=`, `*=` and `/=`.

### Builtin functions

```
// input
btn(i) // is button pressee
btnp(i) // pressed this frame

// graphics
cls(c) // clear screen (0 or 1)
pset(x, y, c) // set a pixel
rectfill(x, y, w, h, c) // filled rectangle
line(x0, y0, x0, y1, c) // line
print(x, y, string) // draw text
```

### Input

Buttons are represented as integers:
- `0`: left
- `1`: right
- `2`: up
- `3`: down
- `4`: A
- `5`: B

### Strings

Strings represent text. They are delimited by double quotes (`"`) and contain ASCII characters (32-127 range) or escape sequences:
- `//` represents char `/`
- `/"` represents char `"`

