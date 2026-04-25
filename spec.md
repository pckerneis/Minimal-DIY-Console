# Minimal DIY Console — Technical Specification

> **Status: Draft / Work in Progress**
> This document is the authoritative specification for the Minimal DIY Console platform.
> Sections marked 🔲 are placeholders pending design decisions.
> Sections marked ✅ are considered stable.

**Spec version:** 0.1  
**Last updated:** 2026-04-23

---

## Contents

1. [Glossary](#1-glossary)
2. [Language Specification](#2-language-specification)
3. [API Reference](#3-api-reference)
4. [Cartridge Format](#4-cartridge-format)
5. [Runtime & Firmware Specification](#5-runtime--firmware-specification)
6. [Memory Layout](#6-memory-layout)
7. [Hardware Specification](#7-hardware-specification)

---

## 1. Glossary

| Term | Definition |
|---|---|
| **Cart** | A program written for the console, consisting of source code and optional metadata |
| **Runtime** | The host environment that parses, executes, and provides built-in functions to a cart |
| **Firmware** | The software flashed to the Raspberry Pi Pico that implements the runtime |
| **Emulator** | Any runtime implementation not running on the reference hardware |
| **Frame** | One execution cycle of `update()` and `draw()`, targeting 30 per second |
| **t** | The absolute audio sample index passed to `audio(t)`, incrementing at 22 050 Hz |

---

## 2. Language Specification

### 2.1 Overview

The console scripting language is a minimal, dynamically-typed, integer-only language with a syntax loosely inspired by JavaScript. It is designed to be implementable in under 2 000 lines of host code.

Key properties:
- Integer-only arithmetic (no floating-point)
- Global variable scope only
- No heap allocation exposed to the programmer
- In V1, no user-defined functions beyond the four lifecycle hooks
- Single-file programs

### 2.2 Grammar

🔲 _TODO_

```
program      := statement*
statement    := assignment | if_stmt | while_stmt | for_stmt | break_stmt | continue_stmt | call_stmt
assignment   := IDENT ('=' | '+=' | '-=' | '*=' | '/=') expr
if_stmt      := 'if' '(' expr ')' block ( 'else' block )?
while_stmt   := 'while' '(' expr ')' block
for_stmt     := 'for' '(' assignment ';' expr ';' assignment ')' block
break_stmt   := 'break'
continue_stmt := 'continue'
call_stmt    := IDENT '(' arglist ')'
block        := '{' statement* '}'
expr         := logical ( ('&&' | '||') logical )*
logical      := comparison ( ('&' | '|' | '^') comparison )*
comparison   := shift ( ('==' | '!=' | '>' | '<' | '>=' | '<=') shift )*
shift        := additive ( ('>>' | '<<') additive )*
additive     := multiplicative ( ('+' | '-') multiplicative )*
multiplicative := unary ( ('*' | '/' | '%') unary )*
unary        := '-' unary | primary
primary      := NUMBER | STRING | IDENT | IDENT '(' arglist ')' | '(' expr ')'
arglist      := (expr (',' expr)*)?
```

### 2.3 Types

The language has two types:

| Type | Description |
|---|---|
| **Integer** | Signed 32-bit. Arithmetic wraps on overflow (two's complement). |
| **String** | Immutable sequence of ASCII characters (codes 32–127). |

There is no boolean type. Integers are used for truthiness: `0` is falsy, all other integers are truthy. Strings are always truthy.

### 2.4 Integer Semantics

- All numeric literals are integers.
- No floating-point values exist at any level visible to the cart.
- Arithmetic is 32-bit signed with wraparound on overflow.
- Division is integer division, truncating toward zero.
- Division by zero returns `0`.

### 2.5 Operator Precedence

From highest to lowest:

| Precedence | Operators | Associativity |
|---|---|---|
| 7 | Unary `-` | Right |
| 6 | `*` `/` `%` | Left |
| 5 | `+` `-` | Left |
| 4 | `>>` `<<` | Left |
| 3 | `>` `<` `>=` `<=` `==` `!=` | Left |
| 2 | `&` `\|` `^` | Left |
| 1 | `&&` `\|\|` | Left |

`&&` and `||` are short-circuit operators: the right-hand side is not evaluated if the result is determined by the left-hand side.

### 2.6 Variables

- All variables are global.
- A variable is created on its first assignment.
- Reading an uninitialised variable returns `0` (integer) or `""` (if a string is expected). In practice, all uninitialised variables default to `0`.
- Variable names: ASCII letters, digits, and `_`; must start with a letter or `_`.
- Maximum of **64 simultaneous global variables** per cart.

Assignment operators: `=` `+=` `-=` `*=` `/=`

### 2.7 Control Flow

Braces are mandatory for all blocks.

```
if (condition) {
  ...
}

if (condition) {
  ...
} else {
  ...
}

while (condition) {
  ...
}

for (i = 0; i < 10; i += 1) {
  ...
}
```

`break` exits the innermost loop immediately. `continue` skips to the next iteration. Both are only valid inside `while` or `for` blocks.

### 2.8 Strings

- Delimited by double quotes: `"hello"`
- ASCII characters only (codes 32–127)
- Escape sequences:
  - `\\` — literal backslash
  - `\"` — literal double quote
- Strings are immutable.
- Concatenation uses the `+` operator: `"hello" + " " + "world"` → `"hello world"`
- `len(s)` returns the length of string `s` as an integer.
- `char(s, i)` returns the character at index `i` (0-based) as a single-character string. Returns `""` if `i` is out of bounds.
- Maximum length of 128 characters. Extra characters are discarded.

### 2.9 Comments

Single-line only, introduced by `//`. Everything from `//` to end of line is ignored.

### 2.10 Lifecycle Functions

A cart may define any combination of the following four functions. All are optional.

```
init()
update(frame, input)
draw(frame, input)
audio(t)
```

See [Section 5 — Runtime Specification](#5-runtime--firmware-specification) for calling semantics.

User-defined functions beyond these four are not supported in v1.

---

## 3. API Reference

### 3.1 Conventions

- `c` is a colour value: `0` = black (off), `1` = white (on).
- `x`, `y` are pixel coordinates. Origin is the **top-left corner** of the screen. X increases rightward, Y increases downward.
- Screen bounds: `x` ∈ [0, 127], `y` ∈ [0, 63].
- Drawing outside screen bounds is silently ignored (clipped).
- All arguments are integers unless otherwise noted.

### 3.2 Input

```
btn(i)   → integer
```
Returns `1` if button `i` is currently held, `0` otherwise. Returns `0` if `i` is out of range [0, 5].

```
btnp(i)  → integer
```
Returns `1` if button `i` was pressed on this frame (edge trigger, not held), `0` otherwise.

**Button indices:**

| Index | Button |
|---|---|
| 0 | Left |
| 1 | Right |
| 2 | Up |
| 3 | Down |
| 4 | A |
| 5 | B |

### 3.3 Graphics

```
cls(c)
```
Clear the entire screen to colour `c`.

```
pset(x, y, c)
```
Set the pixel at `(x, y)` to colour `c`.

```
rectfill(x, y, w, h, c)
```
Draw a filled rectangle. `(x, y)` is the top-left corner; `w` and `h` are width and height in pixels.

```
line(x0, y0, x1, y1, c)
```
Draw a line from `(x0, y0)` to `(x1, y1)`.

```
print(x, y, string)
```
Render `string` starting at pixel `(x, y)`.

- Font: **Monogram** by Datagoblin. Glyph cell is 5×5px for lowercase, with a 2px ascender zone and 2px descender zone, giving a full character height of 9px. Full ASCII printable range supported.
- Characters rendered outside screen bounds are silently clipped.

### 3.4 Math Utilities

```
abs(x)         → integer   // absolute value
min(a, b)        → integer   // smaller of a and b
max(a, b)        → integer   // larger of a and b
clamp(x, lo, hi) → integer   // clamp x between lo and hi (included)

seed(n)          → void      // sets random seed
rnd(n)           → integer   // random integer in [0, n−1]
```

### 3.5 Audio Utilities

No dedicated audio built-ins are planned for v1. Audio synthesis is expected to be done using integer math directly inside `audio(t)`. The math utility functions (`abs`, `min`, `max`, etc.) are available inside `audio(t)`.

### 3.6 Persistence

Each cart has a dedicated save block of **4 × 32-bit integer slots**, persisted to flash and identified by the cart's `@id` metadata field.

**API:**

```
save(slot, value)   // write integer value to slot [0–3]
load(slot)          // read slot [0–3]; returns 0 if never written or slot is out of range
```

- `slot` must be an integer in [0–3].
- `save()` with an out-of-range slot is a silent no-op.
- `load()` with an out-of-range slot returns `0`.
- Values are 32-bit signed integers, consistent with the rest of the language.

**Cart identity:**

Each cart must declare a unique ID in its metadata block:

```
// @id my-cart-v1
```

- The runtime uses `@id` to locate the cart's save block in flash.
- If `@id` is absent and `save()` or `load()` are called, the compiler emits a warning and persistence is disabled at runtime.
- Duplicate `@id` values across carts result in shared save data — the author is responsible for uniqueness.

**Storage layout:**

With a maximum of 32 cart slots and 4 × 4-byte values each, total save data is **512 bytes**, fitting within a single 4 KB flash erase page. One page is dedicated exclusively to save data, laid out as a flat array of 32 entries of 16 bytes each, indexed by a hash of `@id`.

**Flash write strategy:**

`save()` writes to a RAM mirror immediately. The mirror is flushed to flash on:

- **Cart exit** (switching to another cart via the on-device menu)
- **Graceful shutdown** (soft reset or power button, if available)
- **USB connect** (before the storage interface is mounted, to avoid flash contention)

Power loss between a `save()` call and a flush will result in the last unsaved values being lost. This is acceptable for v1.

### 3.7 Cart utilities

Cart Utilities allow to inspect and load available cart files. This allows multi-cart programs or cart loaders. The default cart loader is built with this API.

**API:**

```
cartcount()         // number of available cart files
cartmeta(i, field)  // returns the string value of the requested metadata field or empty string for invalid cart index or non-existent field
loadcart(i)         // if cart at index exists, exit current cart and load the requested cart; returns 0 otherwise
```

---

## 4. Cartridge Format

### 4.1 File Extension

🔲 _TBD_

### 4.2 Encoding

Carts have two representations:

- **Source format** — UTF-8 plain text, used for authoring and sharing.
- **Compiled format** — a binary opcode file produced by the reference compiler, used for distribution and execution on hardware.

The runtime on the Raspberry Pi Pico executes compiled carts only. The web emulator may accept either.

### 4.3 Structure

A v1 cart file consists of two sections in order:

```
[metadata block]   -- optional
[source code]      -- required
```

### 4.4 Metadata Block

The metadata block, if present, must appear at the very top of the file. It is a contiguous sequence of comment lines of the form:

```
// @key value
```

Defined keys:

| Key | Type | Description |
|---|---|---|
| `title` | string | Display name of the cart |
| `author` | string | Author name or handle |
| `version` | string | Semantic version (e.g. `1.0.0`) |
| `desc` | string | Short description (one line) |
| `id` | string | ID used for state persistence |


Example:

```
// @title  Pong
// @author yourname
// @version 1.0.0
// @desc   A minimal Pong implementation

init() {
  ...
}
```

Unknown `@keys` are ignored by the runtime.

### 4.5 Size Limit

🔲 _TBD — maximum cart source size in bytes. Example: 65 536 bytes (64 KB)._

### 4.6 Compiled Cart Format

The compiled format is a binary file produced by the reference compiler from a source cart.

**File extension:** 🔲 _TBD — candidates: `.bin`, `.cbin`, `.cco` (compiled cart opcodes)._

Structure:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 bytes | Magic: 🔲 _TBD (e.g. `MDCC`)_ |
| 4 | 1 byte | Format version |
| 5 | 🔲 TBD | Metadata block (title, author, etc.) |
| 🔲 | remainder | Opcode stream |

🔲 _TBD — opcode set design (stack machine vs register machine, instruction width, etc.)._

### 4.7 Distribution & Flashing

Firmware and carts are deployed separately via two distinct mechanisms.

**Firmware flashing (BOOTSEL mode):**
- To flash a new firmware version, hold the BOOTSEL button on the Pico while connecting it via USB.
- The device appears as a USB mass storage drive on the host.
- Drop the firmware `.uf2` file onto the drive. The device reboots automatically and runs the new firmware.
- This step is only required when updating the runtime itself, not when installing carts.

**Cart installation (storage mode):**
- When connected via USB during normal operation (no BOOTSEL), the device exposes a USB mass storage interface listing the cart storage region of flash as a drive.
- Users drag and drop compiled cart files onto this drive.
- 🔲 *TBD — maximum number of cart slots and per-slot size in flash.*
- On the next boot (or immediately, 🔲 *TBD*), the runtime scans the storage region and makes available all valid compiled carts it finds.

**On-device cart selection:**
- 🔲 *TBD — on-device cart selection UI (boot menu, dedicated button combination, or single-cart-only in v1).*

**Tooling:**
- The reference compiler takes a `.cart` source file and produces a compiled cart binary.
- No packaging step is required to combine firmware and carts — they are deployed independently.
- 🔲 *TBD — whether the compiler is a standalone CLI or integrated into a web-based editor.*

---

## 5. Runtime & Firmware Specification

### 5.1 Boot Sequence

On power-on or reset:

1. Runtime initialises display, input, and audio subsystems.
2. If a cart named "boot.ccard" exists, load it. Else load built-in cart loader.
3. Cart source is loaded and parsed.
4. On parse error: 🔲 _TBD — display error message, halt, or reset._
5. Global variable table is initialised (empty).
6. `init()` is called once, if defined.
7. Main loop begins.

### 5.2 Main Loop

The runtime targets **30 frames per second**. Each frame:

1. Poll input state.
2. Call `update(frame, input)` if defined.
3. Call `draw(frame, input)` if defined.
4. Flush display buffer to screen.
5. Increment `frame` counter.

**`frame`** is a 32-bit unsigned integer incrementing by 1 each frame. Wraps at 2³²−1.  
**`input`** 🔲 _TBD — is input passed as an argument or only accessible via `btn()`/`btnp()`?_

**Overrun behaviour:** If `update()` + `draw()` take longer than one frame period:  
🔲 _TBD — skip draw, drop frame, or run as fast as possible?_

### 5.3 Audio Callback

`audio(t)` is called by the audio subsystem running on **core 1**, 22 050 times per second (22.05 kHz sample rate).

- `t` is the **absolute sample index** since boot, as a 32-bit integer. Wraps at 2³²−1 (~53 hours of audio).
- The return value of `audio(t)` is the output sample as an **unsigned 8-bit integer in the range [0, 255]**. Values outside this range are clamped.
- `audio(t)` may call math utility functions (`abs`, `min`, `max`, etc.) but may not call any graphics, input, or persistence built-ins.
- `audio(t)` must not write to any global variable. This constraint is enforced by convention in v1; a compile-time check may be added later.

**Global variable access — shadow copy:**

`audio(t)` reads cart globals from a **shadow copy** of the live variable table, not the live table itself. This gives audio a fully consistent end-of-frame snapshot with no risk of torn reads.

- The runtime maintains **two shadow buffers** (A and B), each 256 bytes (64 variables × 4 bytes).
- After each `draw()` call completes, the runtime writes the current live variable table into the inactive buffer, then atomically flips the active buffer index.
- Core 1 always reads from the buffer indicated by the active index. It never sees a partially written snapshot.
- A global variable written in `update()` will be visible to `audio(t)` at the start of the next frame — a maximum latency of one frame (~33ms). This is acceptable for reactive audio.

**Core assignment:**

| Core | Responsibilities |
|---|---|
| Core 0 | Main loop: `init()`, `update()`, `draw()`, display flush, USB, input polling |
| Core 1 | Audio callback: `audio(t)`, DAC/PWM output |

🔲 *TBD — define behaviour if `audio(t)` execution exceeds one sample period (i.e. takes longer than ~45µs). Options: output silence for the overrun sample, repeat the previous sample, or halt with a runtime error.*
 
### 5.4 Error Handling

🔲 _TBD. Example:_
- **Parse error at boot:** Display error message with line number; halt.
- **Runtime error:** Display error message with call stack and line number; halt.
- **Stack overflow:** 🔲 _TBD._

### 5.5 Display

- Resolution: **128 × 64 pixels**, 1-bit colour (monochrome).
- The framebuffer is written during `draw()` and flushed to the screen after `draw()` returns.
- Graphics calls (`pset`, `rectfill`, etc.) are permitted outside of `draw()` (e.g. inside `update()`). They write to the framebuffer immediately but the result will not be visible until the next flush.

---

## 6. Memory Layout

> This section describes the reference Raspberry Pi Pico target.
> Emulators are not bound by these constraints but should aim to respect the variable and string limits.

### 6.1 Raspberry Pi Pico Memory Budget

The RP2040 provides **264 KB SRAM** total.

🔲 _TBD — full allocation pending firmware implementation._

| Region | Size | Notes |
|---|---|---|
| Firmware / runtime | ~100 KB | Interpreter, built-ins, USB stack |
| Framebuffer | 1 KB | 128×64 × 1 bit |
| Audio ring buffer | 🔲 TBD | |
| Cart opcode buffer | 🔲 TBD | Compiled cart loaded from flash |
| Variable table | ~1 KB | 64 variables × (name + value) |
| String storage | 🔲 TBD | Interned or stack-allocated |
| Call stack | 🔲 TBD | |
| Free / reserved | remainder | |

### 6.2 Variable Table

Maximum **64 global variables** per cart. Each variable stores either a 32-bit integer or a string reference.

### 6.3 String Storage

All strings in a cart are **literals known at compile time**. The compiler collects every unique string literal from the source and stores them in a **string table** embedded in the compiled cart binary. At runtime, string values are references into this table — no heap allocation occurs.
- **Maximum string length:** 128 characters. Literals exceeding this limit are silently truncated to their first 128 characters at compile time.
- **Maximum unique string literals per cart:** 32. The compiler errors if this limit is exceeded.
- **Concatenation:** The + operator and char() produce new strings at runtime. Results are capped at 128 characters; excess characters are discarded.

**Runtime string allocation — scratch buffer:**

Runtime-produced strings are allocated from a **fixed scratch buffer**: a small pool of slots (e.g. 8 × 128 bytes) managed as a stack. This approach is recommended over runtime interning for three reasons:
- Hardware fit: memory usage is static and fully predictable at compile time.
- Implementation simplicity: no deduplication logic, no hash map, no table mutation at runtime.
- Function call compatibility: if user-defined functions are added later, scratch slots can be scoped to the call frame and released automatically on return, with no GC or ref-counting required.

The main constraint is that dynamic strings must not be held across frames or call boundaries. Given the current feature set this is acceptable, and can be enforced by convention or a future linting pass.

🔲 TBD — exact number of scratch slots and eviction behaviour when the pool is exhausted (silent truncation, runtime error, or oldest-slot reuse).

### 6.4 Call Stack

🔲 _TBD — maximum call depth. Suggested: 32 frames._

---

## 7. Hardware Specification

> This section defines the reference hardware for firmware and PCB design.
> Emulators may ignore this section.

### 7.1 Microcontroller

- **Target:** Raspberry Pi Pico (RP2040)
- Clock speed: 🔲 _TBD — default 125 MHz or overclocked._
- Flash: 2 MB (cart storage and firmware)

### 7.2 Display

- Resolution: 128 × 64 pixels, monochrome
- Interface: 🔲 _TBD — SPI or I²C. Recommend SPI for speed._
- Compatible controllers: SSD1306 (OLED), ST7565 (LCD)
- Pinout: 🔲 _TBD_

### 7.3 Input

- 6 tact switches, momentary normally-open
- Wiring: 🔲 _TBD — active-low with internal pull-up, or external pull-down._
- Debounce: handled in firmware (🔲 _TBD — debounce interval in ms_)
- GPIO assignments: 🔲 _TBD_

### 7.4 Audio Output

- Sample rate: 22 050 Hz
- Bit depth: 8
- Output method: PWM with RC filter
- Output impedance target: suitable for 1W 8Ω speaker or 3.5mm line out
- RC filter values: 🔲 _TBD_
- GPIO assignment: 🔲 _TBD_

### 7.5 Power

- Supply: USB (5V via Pico onboard regulator to 3.3V)
- Battery operation: 3× AAA 
- Estimated current draw: 🔲 _TBD_

### 7.6 Pinout Summary

🔲 _TBD — full GPIO assignment table once display interface and audio method are decided._

---

## Appendix A — Open Questions

A consolidated list of decisions that need to be made before this spec is considered stable.

| # | Section | Question |
|---|---|---|
| 5 | 4.1 | File extension for source and compiled carts |
| 6 | 4.5 | Max cart source size |
| 7 | 4.6 | Opcode set design (stack vs register machine, instruction width) |
| 8 | 4.7 | Flash memory layout; cart slot size; on-device cart selection UI |
| 9 | 5.2 | `input` argument encoding; overrun behaviour |
| 11 | 5.4 | Runtime error behaviour |
| 12 | 6.1 | Full SRAM allocation |
| 13 | 6.3 | String storage strategy and maximum string length |
| 14 | 6.4 | Call stack depth |
| 15 | 7.2 | Display interface (SPI vs I²C) and pinout |
| 16 | 7.3 | Button wiring and debounce interval |
| 17 | 7.4 | Audio output method and RC filter values |
