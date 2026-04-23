# Minimal DIY Console

> A tiny, open, DIY platform for making games, art, and apps — from hardware to software.

Minimal DIY Console is a creative and educational fantasy console designed to run on minimal hardware (a Raspberry Pi Pico). It pairs a monochrome 128×64 screen, 6-button input, and procedural audio with an intentionally small scripting language, so that a single person can understand and build the entire stack.

The constraints are the point: no floats, no sprites, no assets — just code, pixels, and sound.

> ⚠️ This project is in early development. A web emulator and Pico prototype are on the roadmap. Watch the repo to follow progress.

---

## Contents

- [Philosophy](#philosophy)
- [Example cart](#example-cart)
- [Program structure](#program-structure)
- [Scripting language](#scripting-language)
- [Hardware](#hardware)
- [Roadmap](#roadmap)
- [Contributing](#contributing)
- [Prior art](#prior-art)

---

## Philosophy

Most creative tools hide their complexity. This one doesn't.

Minimal DIY Console is designed so that a curious person — a student, a hobbyist, a tinkerer — can trace every part of the system: the hardware schematic, the interpreter, the draw loop, and the audio callback. Building something on it means understanding it.

Constraints are deliberate:
- **128×64 monochrome display** — forces spatial thinking
- **6 buttons only** — forces interaction design
- **Integer-only math** — keeps the interpreter tiny and auditable
- **No asset pipeline** — everything is procedural or drawn in code

---

## Example cart

A complete Pong implementation in ~50 lines:

```
init() {
  bx = 64  // ball position
  by = 32
  bvx = 1  // ball velocity
  bvy = 1

  py = 28  // player paddle Y
  ey = 28  // enemy paddle Y
}

update(frame, input) {
  // move player paddle
  if (btn(2)) py -= 2
  if (btn(3)) py += 2
  if (py < 0) py = 0
  if (py > 54) py = 54

  // move ball
  bx += bvx
  by += bvy

  // wall bounce
  if (by < 0 || by > 63) bvy = -bvy

  // player paddle collision
  if (bx < 6 && by >= py && by <= py + 10) bvx = 1

  // enemy AI
  if (by > ey + 5) ey += 1
  if (by < ey + 5) ey -= 1

  // enemy paddle collision
  if (bx > 122 && by >= ey && by <= ey + 10) bvx = -1

  // reset on out of bounds
  if (bx < 0 || bx > 127) {
    bx = 64
    by = 32
    bvx = -bvx
  }
}

draw() {
  cls(0)
  rectfill(2, py, 4, 10, 1)    // player paddle
  rectfill(122, ey, 4, 10, 1)  // enemy paddle
  rectfill(bx, by, 2, 2, 1)    // ball
}
```

---

## Program structure

A cart defines up to four lifecycle functions:

| Function | Called | Purpose |
|---|---|---|
| `init()` | Once at start | Initialise state |
| `update(frame, input)` | 30× per second | Game logic |
| `draw(frame, input)` | 30× per second | Rendering |
| `audio(t)` | 22 050× per second | Sound synthesis |

All functions are optional. A cart that only defines `draw()` is valid.

---

## Scripting language

The console uses a minimal scripting language with a syntax close to JavaScript. It is integer-only (32-bit wraparound) with no heap allocation and no floating-point.

### Operators

```
+ - * / %          arithmetic
> < >= <= ==       comparison
& | ^ >> <<        bitwise
```

### Variables

Variables are global and created on first assignment. No declaration keyword needed.

```
x = 10
x += 1
x -= 1
x *= 2
x /= 2
```

### Control flow

```
if (condition) { ... }
if (condition) { ... } else { ... }
while (condition) { ... }
```

### Strings

Strings are ASCII text (characters 32–127) delimited by double quotes.

Escape sequences:
- `\\` — literal `\` character
- `\"` — literal `"` character

### Built-in functions

#### Input

```
btn(i)   // returns 1 if button i is held
btnp(i)  // returns 1 if button i was pressed this frame
```

Button indices:

| Index | Button |
|---|---|
| 0 | Left |
| 1 | Right |
| 2 | Up |
| 3 | Down |
| 4 | A |
| 5 | B |

#### Graphics

```
cls(c)                  // clear screen; c is 0 (black) or 1 (white)
pset(x, y, c)           // set pixel at (x, y) to colour c
rectfill(x, y, w, h, c) // filled rectangle
line(x0, y0, x1, y1, c) // draw a line
print(x, y, string)     // render text
```

Screen dimensions: 128 × 64 pixels.

#### Persistence

_Planned — not yet implemented._ State save/load will be addressed before v1.

---

## Hardware

### Minimal bill of materials

| Component | Notes |
|---|---|
| Raspberry Pi Pico | RP2040-based board |
| 128×64 display | OLED or LCD; SPI or I²C |
| 6× tact switch | Momentary pushbutton |
| Speaker or jack | 1W 8Ω speaker or 3.5mm female connector |
| Breadboard + jumper wires | For prototyping; a PCB design is planned |

Wiring instructions and a PCB layout are on the roadmap. A breadboard prototype is sufficient to get started.

---

## Roadmap

### v1

| Item | Status |
|---|---|
| Language specification | 📋 Planned |
| Raspberry Pi Pico prototype | 📋 Planned |
| Reference compiler / interpreter | 📋 Planned |
| Web emulator | 📋 Planned |
| Pico build instructions | 📋 Planned |
| PCB files | 📋 Planned |
| Enclosure design | 📋 Planned |

### Beyond v1

- Community forum
- Cart sharing platform
- Build and BOM sharing platform

---

## Contributing

The project is open and contributions are welcome, even at this early stage. The most useful things right now are:

- Feedback on the language design and API
- Breadboard prototype builds and reports
- Web emulator implementation

Please open an issue before starting significant work so we can coordinate.

---

## Prior art

Minimal DIY Console draws direct inspiration from:

- [PICO-8](https://www.lexaloffle.com/pico-8.php) — the original fantasy console
- [TIC-80](https://tic80.com/) — open-source fantasy computer
- [Lowres NX](https://lowresnx.inutilis.com/) — educational fantasy console
- [Arduboy](https://www.arduboy.com/) — open-source handheld gaming platform

The goal is not to replicate these but to push further toward hardware openness and interpretive minimalism.
