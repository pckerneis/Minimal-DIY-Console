# Bidule 01 — Technical Specification

> **Status: Draft / Work in Progress**
> This document is the authoritative specification for the Bidule 01 platform.
> Sections marked 🔲 are placeholders pending design decisions.
> Sections marked ✅ are considered stable.

**Spec version:** 0.2  
**Last updated:** 2026-04-28

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
statement    := assignment | incr_stmt | if_stmt | while_stmt | for_stmt | break_stmt | continue_stmt | call_stmt
assignment   := IDENT ('=' | '+=' | '-=' | '*=' | '/=') expr
incr_stmt    := IDENT ('++' | '--') | ('++' | '--') IDENT
if_stmt      := 'if' '(' expr ')' block ( 'else' block )?
while_stmt   := 'while' '(' expr ')' block
for_stmt     := 'for' '(' assignment ';' expr ';' (assignment | incr_stmt) ')' block
break_stmt   := 'break'
continue_stmt := 'continue'
call_stmt    := IDENT '(' arglist ')'
block        := '{' statement* '}'
expr         := bitor ( ('&&' | '||') bitor )*
bitor        := bitxor ( '|' bitxor )*
bitxor       := bitand ( '^' bitand )*
bitand       := comparison ( '&' comparison )*
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
| 9 | Unary `-` | Right |
| 8 | `*` `/` `%` | Left |
| 7 | `+` `-` | Left |
| 6 | `>>` `<<` | Left |
| 5 | `>` `<` `>=` `<=` `==` `!=` | Left |
| 4 | `&` | Left |
| 3 | `^` | Left |
| 2 | `\|` | Left |
| 1 | `&&` `\|\|` | Left |

`&&` and `||` are short-circuit operators: the right-hand side is not evaluated if the result is determined by the left-hand side.

### 2.6 Variables

- All variables are global.
- A variable is created on its first assignment.
- All uninitialised variables default to `0`.
- Variable names: ASCII letters, digits, and `_`; must start with a letter or `_`.
- Maximum of **64 simultaneous global variables** per cart.

Assignment operators: `=` `+=` `-=` `*=` `/=`

Increment/decrement: `++` `--` (statement form only; both prefix `++i` and postfix `i++` are equivalent and do not return a value)

### 2.7 Control Flow

Braces are mandatory for multi-statement blocks. They can be omitted
for single statement branches.

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

for (i = 0; i < 10; i++) {
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
print(text, x, y, c)
```
Render `text` starting at pixel `(x, y)` in colour `c` on a single line (no text wrap and no line breaks). Integer values are converted to their decimal string representation.

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

Each cart has a dedicated save block of **4 × 32-bit integer slots**, persisted to flash
and identified by the cart's `@id` metadata field.

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

- `@id` must be 1–32 ASCII printable characters (codes 32–126). Longer values are a
  compile-time error.
- If `@id` is absent and `save()` or `load()` are called, the compiler emits a warning
  and persistence is disabled at runtime.
- Uniqueness is the author's responsibility. Two carts with identical `@id` values share
  the same save entry.

**Storage layout:**

Save data occupies a single dedicated 4 KB flash erase page. It is structured as a flat
array of **32 entries**, each 48 bytes:

| Offset | Size | Field |
|---|---|---|
| 0 | 32 B | `id` — the cart's `@id` string, null-padded to 32 bytes |
| 32 | 16 B | `values` — 4 × 32-bit signed integers, little-endian |

Total: 32 × 48 = 1 536 bytes, comfortably within one 4 KB page.

An entry is considered **free** if its first byte is `0x00`.

**Save entry lookup:**

On any `save()` or `load()` call, the runtime performs a linear scan (at most 32
iterations) over the save page:

1. If an entry whose `id` field matches the current cart's `@id` is found, that entry
   is used.
2. If no match is found and the operation is `load()`, return `0`.
3. If no match is found and the operation is `save()`, allocate the first free entry,
   write the `@id` into its `id` field, then write the value.
4. If no match is found, no free entry exists, and the operation is `save()`, the call
   is a silent no-op. A runtime warning is displayed if a warning output channel is
   available.

**Flash write strategy:**

`save()` writes to a RAM mirror immediately. The mirror is flushed to flash on:

- **Cart exit** (switching to another cart via the on-device menu)
- **Graceful shutdown** (soft reset or power button, if available)
- **USB connect** (before the storage interface is mounted, to avoid flash contention)

Power loss between a `save()` call and a flush will result in the last unsaved values
being lost. This is acceptable for v1.

**Entry lifecycle:**

There is no `delete` operation in v1. Once a save entry is allocated for a given `@id`,
it persists until the save page is manually erased (e.g. by a firmware-level reset
utility). Future versions may introduce an explicit `clearsave()` built-in.

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

| Format | Extension |
|---|---|
| Source cart | `.bdcart` |
| Compiled cart | `.bdb` |

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

- Maximum cart **source** size: **65 536 bytes** (64 KB).
- Maximum compiled **bytecode** size: **16 384 bytes** (16 KB).
- Maximum unique **string literals** per cart: 32 (see §6.3).

### 4.6 Compiled Cart Format

The compiled format is a binary file produced by the reference compiler from a source cart.

**File extension:** `.bdb`

#### Binary layout

All multi-byte integers are little-endian.

| Offset | Size | Field |
|---|---|---|
| 0 | 4 B | Magic: `B` `D` `B` `N` |
| 4 | 1 B | Format version: `1` |
| 5 | 1 B | Flags: `0` (reserved) |
| 6 | 2 B | Metadata block length _N_ |
| 8 | _N_ B | Metadata block (raw text, ignored by runtime) |
| 8+_N_ | 1 B | String count (0–32) |
| … | … | String table: for each entry: `[len: u8][chars: len bytes]` (not null-terminated) |
| … | 2 B | `init_off` — bytecode offset of `init()` body (`0xFFFF` = not defined) |
| … | 2 B | `update_off` |
| … | 1 B | `update` `frame` parameter slot (global variable index, `0xFF` = not bound) |
| … | 1 B | `update` `input` parameter slot |
| … | 2 B | `draw_off` |
| … | 1 B | `draw` `frame` parameter slot |
| … | 1 B | `draw` `input` parameter slot |
| … | 2 B | `audio_off` |
| … | 1 B | `audio` `t` parameter slot |
| … | remainder | Bytecode stream |

Entry-point offsets are byte offsets from the start of the bytecode stream. All four entry-point records (init, update, draw, audio) are always present in the header; unused ones are set to `0xFFFF`.

**Parameter slots:** each lifecycle function that accepts parameters (`update(frame, input)`, `draw(frame, input)`, `audio(t)`) declares the global variable slots those parameters are bound to. Before executing the function, the runtime writes the argument values into those slots. A slot of `0xFF` means the parameter name is not referenced in the function body and requires no pre-assignment.

#### Opcode set

The VM is a **stack-based interpreter**. Instructions use variable-width encoding: a 1-byte opcode followed by zero or more inline operands. Jump offsets are signed 16-bit integers relative to the instruction immediately following the operand.

| Opcode | Hex | Operands | Description |
|---|---|---|---|
| `PUSH_INT` | `0x00` | `i32` | Push 32-bit integer constant |
| `PUSH_STR` | `0x01` | `u8` | Push string literal by table index (0–31) |
| `LOAD` | `0x02` | `u8` | Push global variable by slot (0–63) |
| `STORE` | `0x03` | `u8` | Pop → global variable slot |
| `ADD` | `0x10` | — | Pop b, pop a; push `a + b` (string concatenation if either is a string) |
| `SUB` | `0x11` | — | Push `a - b` |
| `MUL` | `0x12` | — | Push `a * b` |
| `DIV` | `0x13` | — | Push `a / b`; push `0` if `b == 0` |
| `MOD` | `0x14` | — | Push `a % b`; push `0` if `b == 0` |
| `NEG` | `0x15` | — | Pop a; push `-a` |
| `BAND` | `0x20` | — | Push `a & b` |
| `BOR` | `0x21` | — | Push `a \| b` |
| `BXOR` | `0x22` | — | Push `a ^ b` |
| `SHL` | `0x23` | — | Push `a << (b & 31)` |
| `SHR` | `0x24` | — | Push `a >> (b & 31)` |
| `EQ` | `0x30` | — | Push `1` if `a == b`, else `0` (string content comparison) |
| `NE` | `0x31` | — | Push `1` if `a != b`, else `0` |
| `LT` | `0x32` | — | Push `1` if `a < b`, else `0` |
| `LE` | `0x33` | — | Push `1` if `a <= b`, else `0` |
| `GT` | `0x34` | — | Push `1` if `a > b`, else `0` |
| `GE` | `0x35` | — | Push `1` if `a >= b`, else `0` |
| `NOT` | `0x36` | — | Pop a; push `1` if `a == 0`, else `0`. Two in sequence normalise any value to `0` or `1`. |
| `POP` | `0x40` | — | Discard top of stack |
| `JUMP` | `0x50` | `i16` | Unconditional relative jump |
| `JUMP_T` | `0x51` | `i16` | Pop; jump if nonzero |
| `JUMP_F` | `0x52` | `i16` | Pop; jump if zero |
| `PEEK_JUMP_T` | `0x53` | `i16` | Peek (no pop); jump if nonzero — used for `\|\|` short-circuit |
| `PEEK_JUMP_F` | `0x54` | `i16` | Peek (no pop); jump if zero — used for `&&` short-circuit |
| `CALL` | `0x60` | `u8 id`, `u8 argc` | Call built-in `id`; args pushed left-to-right; pops `argc` args; pushes return value unless void |
| `RET` | `0xFF` | — | Return from lifecycle function |

#### Compound assignment compilation

Compound assignments (`+=`, `-=`, `*=`, `/=`) compile to `LOAD slot` + arithmetic opcode + `STORE slot`. No dedicated compound-assignment opcodes exist.

`++` and `--` (both prefix and postfix forms) compile identically to `LOAD slot` + `PUSH_INT 1` + `ADD`/`SUB` + `STORE slot`.

#### Short-circuit compilation

`a && b` compiles to:
```
<eval a>
PEEK_JUMP_F skip   ; if a == 0: leave 0 on stack, jump past b
POP
<eval b>
skip:
NOT NOT            ; normalise result to 0 or 1
```

`a || b` compiles to:
```
<eval a>
PEEK_JUMP_T skip   ; if a != 0: leave a on stack, jump past b
POP
<eval b>
skip:
NOT NOT
```

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
- A maximum of 32 carts can be installed at once.
- On the next boot, the runtime scans the storage region and makes available all valid compiled carts it finds.

**On-device cart selection:**
- The runtime ships with a built-in cart selector that lists all valid `.bdb` carts found in storage. The user navigates and launches a cart from this screen.
- The built-in selector is the default boot experience. It can be replaced by placing a custom `boot.bdb` in cart storage (see §5.1).
- There is no button combination to return to the boot cart while a cart is running. A cart returns to the selector only by calling `loadcart()` with the appropriate index, or via a hardware reset.

**Tooling:**
- The reference compiler takes a `.bdcart` source file and produces a `.bdb` compiled cart binary.
- No packaging step is required to combine firmware and carts — they are deployed independently.
- The reference compiler is a **standalone JavaScript CLI** (`compiler/compiler.js`). It takes a `.bdcart` source file and writes a `.bdb` binary.

---

## 5. Runtime & Firmware Specification

### 5.1 Boot Sequence

On power-on or reset:

1. Runtime initialises display, input, and audio subsystems.
2. If any button is held at boot, the device enters **USB storage mode**: a mass-storage interface is presented over USB and the device loops until unplugged or reset. Normal cart execution does not occur.
3. Attempt to load and validate `boot.bdb` from cart storage.
4. If load or validation fails, display an error message and prompt: _"Press any button to continue."_ Wait for any button press, then load the built-in cart loader instead.
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

**`frame`** is a 32-bit signed integer incrementing by 1 each frame. It wraps from 2³¹−1 (2 147 483 647) back to −2 147 483 648 on overflow. At 30 fps this takes ~828 days, but carts that test `frame > N` or use `frame` in arithmetic should be aware of this wrap.  
**`input`** is a bitfield integer representing the currently held buttons. Bit `i` is set if button `i` is held, matching the indices defined in §3.2:

| Bit | Button |
|---|---|
| 0 | Left |
| 1 | Right |
| 2 | Up |
| 3 | Down |
| 4 | A |
| 5 | B |

Example: `input & 1` tests Left; `input & 16` tests A. `btn()` and `btnp()` remain available as a convenience API on top of this bitfield.

**Overrun behaviour:** If `update()` + `draw()` + display flush take longer than one frame period (33 ms), the next frame starts immediately with no delay. No frames are dropped and `draw()` is never skipped. Under sustained overrun, the effective frame rate falls below 30 fps.

### 5.3 Audio Callback

`audio(t)` is called by the audio subsystem running on **core 1**, 22 050 times per second (22.05 kHz sample rate).

- `t` is the **absolute sample index** since boot, as a 32-bit integer. Wraps at 2³²−1 (~53 hours of audio).
- The return value of `audio(t)` is the output sample as an **unsigned 8-bit integer in the range [0, 255]**. Values outside this range are clamped.
- `audio(t)` may call math utility functions (`abs`, `min`, `max`, etc.) but may not call any graphics, input, or persistence built-ins.
- `audio(t)` must not write to any global variable. This constraint is enforced at compile time.

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

**Audio overrun:** Core 1 uses a busy-wait loop that outputs one sample then spins until 45 µs have elapsed. If `audio(t)` itself takes longer than 45 µs, the busy-wait period shrinks to zero and the next sample starts immediately. No silence is inserted, no error is raised; the effective sample rate degrades proportionally to the overrun.
 
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

| Region | Size | Notes |
|---|---|---|
| Firmware / runtime | ~100 KB | Interpreter, built-ins, USB stack, SDK |
| Cart bytecode buffer | 16 KB | Compiled cart loaded from flash |
| String literal table | ~4 KB | 32 strings × 128 bytes |
| Global variable table (live + 2 audio shadows) | ~2 KB | 3 × 64 vars × ~8 bytes |
| String scratch pool | ~1 KB | 8 slots × 128 bytes |
| Framebuffer | 1 KB | 128×64 × 1 bit (8 pages × 128 bytes) |
| Evaluation stacks | ~512 B | 2 × 32 slots — core 0 and core 1 |
| Free / reserved | remainder | |

Total consumed ≈ ~125 KB, leaving ~140 KB free.

### 6.2 Variable Table

Maximum **64 global variables** per cart. Variable names are resolved to slot indices (0–63) at compile time and are not stored at runtime. Each slot holds a tagged value: a 1-byte type tag (`INT` or `STR`) and a 4-byte payload (32-bit integer, or a string table index). Three copies of the table exist in memory at all times: the live table (core 0) and two shadow copies for lock-free audio reads (see §5.3).

### 6.3 String Storage

All string literals in a cart are known at compile time. The compiler collects every unique string literal from the source and stores them in a **string table** embedded in the compiled cart binary. At runtime, string values are references into this table — no heap allocation occurs.
- **Maximum string length:** 128 characters. Literals exceeding this limit are silently truncated to their first 128 characters at compile time.
- **Maximum unique string literals per cart:** 32. The compiler errors if this limit is exceeded.
- **Concatenation:** The + operator and char() produce new strings at runtime. Results are capped at 128 characters; excess characters are discarded.

**Runtime string allocation — scratch buffer:**

Runtime-produced strings are allocated from a **fixed scratch buffer**: a small pool of slots (e.g. 8 × 128 bytes) managed as a stack. This approach is recommended over runtime interning for three reasons:
- Hardware fit: memory usage is static and fully predictable at compile time.
- Implementation simplicity: no deduplication logic, no hash map, no table mutation at runtime.
- Function call compatibility: if user-defined functions are added later, scratch slots can be scoped to the call frame and released automatically on return, with no GC or ref-counting required.

The main constraint is that dynamic strings must not be held across frames or call boundaries. Given the current feature set this is acceptable, and can be enforced by convention or a future linting pass.

The scratch pool has **8 slots** of 128 bytes each (~1 KB total). The pool is reset (all slots freed) at the start of each lifecycle function call (`init`, `update`, `draw`, `audio`). Allocation uses a ring-buffer strategy: when all 8 slots are in use, the next allocation reuses the oldest slot (overwriting it). Dynamic strings must therefore not be held across lifecycle call boundaries.

### 6.4 Evaluation Stack

The VM uses a per-core operand stack of **32 slots** (one for core 0, one for core 1). Stack overflow is not checked in v1; exceeding 32 operands in a single expression causes undefined behaviour.

There is no traditional call stack in v1: the four lifecycle functions are direct entry points, not called from one another, and user-defined functions are not supported. The "call depth" is therefore always 1.

---

## 7. Hardware Specification

> This section defines the reference hardware for firmware and PCB design.
> Emulators may ignore this section.

### 7.1 Microcontroller

- **Target:** Raspberry Pi Pico (RP2040)
- Clock speed: 🔲 _TBD — default SDK clock is 125 MHz; overclocking not yet evaluated._
- Flash: 2 MB (cart storage and firmware)

### 7.2 Display

- Resolution: 128 × 64 pixels, monochrome
- Interface: I²C at 400 kHz (fast mode)
- Controller: SSD1306 OLED, I²C address `0x3C`
- Pins: SDA = GP4, SCL = GP5

### 7.3 Input

- 6 tact switches, momentary normally-open
- Wiring: active-low, internal pull-up resistors enabled on the Pico
- GPIO assignments:

| Button | GPIO |
|---|---|
| Left | GP6 |
| Right | GP7 |
| Up | GP8 |
| Down | GP9 |
| A | GP10 |
| B | GP11 |

- Debounce: no software debounce in v1 — buttons are sampled once per frame (~33 ms). Mechanical debounce on the PCB is recommended.

### 7.4 Audio Output

- Sample rate: 22 050 Hz
- Bit depth: 8-bit unsigned [0, 255]; 128 = silence (midpoint)
- Output method: PWM with RC filter
- PWM configuration: wrap = 255 (8-bit resolution), clkdiv = 1.0 → carrier ≈ 488 kHz
- Output impedance target: suitable for 1W 8Ω speaker or 3.5mm line out
- GPIO assignment: GP28 (current firmware default)
- RC filter values: 🔲 _TBD_

### 7.5 Power

- Supply: USB (5V via Pico onboard regulator to 3.3V)
- Battery operation: 3× AAA 
- Estimated current draw: 🔲 _TBD_

### 7.6 Pinout Summary

🔲 _TBD — full GPIO assignment table once display interface and audio method are decided._

---

## Appendix A — Open Questions

Remaining decisions before this spec is considered stable.

| # | Section | Question |
|---|---|---|
| 1 | 5.4 | Runtime error behaviour (parse errors, runtime errors, stack overflow) |
| 2 | 7.1 | Clock speed — default 125 MHz or overclocked? |
| 3 | 7.4 | RC filter values for audio output |
