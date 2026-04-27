#pragma once
#include <stdbool.h>
#include <stdint.h>

// ─── Opcodes (§4.6) ──────────────────────────────────────────────────────────
//
// Variable-width encoding: 1-byte opcode followed by inline operands.
//
//   OP_PUSH_INT      [i32 LE]          push 32-bit integer constant
//   OP_PUSH_STR      [u8]              push string literal by table index (0–31)
//   OP_LOAD          [u8]              push global variable by slot (0–63)
//   OP_STORE         [u8]              pop → global variable slot
//   OP_JUMP          [i16 LE]          unconditional jump, offset relative to next instruction
//   OP_JUMP_T        [i16 LE]          pop; jump if nonzero
//   OP_JUMP_F        [i16 LE]          pop; jump if zero
//   OP_PEEK_JUMP_T   [i16 LE]          peek (no pop); jump if nonzero  — short-circuit ||
//   OP_PEEK_JUMP_F   [i16 LE]          peek (no pop); jump if zero     — short-circuit &&
//   OP_CALL          [u8 id][u8 argc]  call built-in; pops argc args; pushes result if non-void
//
// Compound assignments compile to LOAD + op + STORE (no dedicated opcodes).

typedef enum {
    // Literals
    OP_PUSH_INT      = 0x00,   // [i32]
    OP_PUSH_STR      = 0x01,   // [u8]
    // Variables
    OP_LOAD          = 0x02,   // [u8]
    OP_STORE         = 0x03,   // [u8]
    // Arithmetic
    OP_ADD           = 0x10,
    OP_SUB           = 0x11,
    OP_MUL           = 0x12,
    OP_DIV           = 0x13,
    OP_MOD           = 0x14,
    OP_NEG           = 0x15,
    // Bitwise
    OP_BAND          = 0x20,
    OP_BOR           = 0x21,
    OP_BXOR          = 0x22,
    OP_SHL           = 0x23,
    OP_SHR           = 0x24,
    // Comparison — push 0 or 1
    OP_EQ            = 0x30,
    OP_NE            = 0x31,
    OP_LT            = 0x32,
    OP_LE            = 0x33,
    OP_GT            = 0x34,
    OP_GE            = 0x35,
    // Logical NOT: nonzero→0, zero→1  (two in sequence normalise to 0/1)
    OP_NOT           = 0x36,
    // Stack
    OP_POP           = 0x40,
    // Control flow
    OP_JUMP          = 0x50,   // [i16]
    OP_JUMP_T        = 0x51,   // [i16]
    OP_JUMP_F        = 0x52,   // [i16]
    OP_PEEK_JUMP_T   = 0x53,   // [i16]
    OP_PEEK_JUMP_F   = 0x54,   // [i16]
    // Built-in call
    OP_CALL          = 0x60,   // [u8 id][u8 argc]
    // Return from lifecycle function
    OP_RET           = 0xFF,
} Opcode;

// ─── Built-in function IDs (§3.2–3.7) ────────────────────────────────────────

typedef enum {
    // Input (§3.2)
    BUILTIN_BTN        = 0,
    BUILTIN_BTNP       = 1,
    // Graphics (§3.3)
    BUILTIN_CLS        = 2,
    BUILTIN_PSET       = 3,
    BUILTIN_RECTFILL   = 4,
    BUILTIN_LINE       = 5,
    BUILTIN_PRINT      = 6,
    // Math (§3.4)
    BUILTIN_ABS        = 7,
    BUILTIN_MIN        = 8,
    BUILTIN_MAX        = 9,
    BUILTIN_CLAMP      = 10,
    BUILTIN_SEED       = 11,
    BUILTIN_RND        = 12,
    // Strings (§2.8)
    BUILTIN_LEN        = 13,
    BUILTIN_CHAR       = 14,
    // Persistence (§3.6)
    BUILTIN_SAVE       = 15,
    BUILTIN_LOAD_SLOT  = 16,
    // Cart utilities (§3.7)
    BUILTIN_CARTCOUNT  = 17,
    BUILTIN_CARTMETA   = 18,
    BUILTIN_LOADCART   = 19,
} BuiltinId;

// ─── Public API ───────────────────────────────────────────────────────────────

// Load a compiled cart binary (.bdb). Returns false if the binary is invalid.
bool vm_load(const uint8_t *bin, uint32_t len);

// Lifecycle hooks — no-ops if vm_load has not succeeded.
void vm_call_init(void);
void vm_call_update(int frame, uint8_t input);
void vm_call_draw(int frame, uint8_t input);
int  vm_call_audio(int t);   // returns sample in [0, 255]

// Returns true (and clears the flag) if loadcart() was called this frame.
// When true, the caller should reset its frame counter to 0.
bool vm_cart_switched(void);

// Copy live globals into the inactive audio shadow buffer and flip the active
// index. Call this from core 0 immediately after vm_call_draw() returns.
void vm_sync_audio_shadow(void);
