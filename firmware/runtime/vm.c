#include "vm.h"
#include "../display.h"
#include "../input.h"
#include "../cart.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// ─── Limits ───────────────────────────────────────────────────────────────────

#define MAX_VARS      64
#define MAX_STRINGS   32
#define MAX_STR_LEN   128
#define MAX_BYTECODE  16384
#define STACK_SIZE    32
#define SCRATCH_SLOTS 8
#define SCRATCH_BASE  32   // scratch indices occupy [32, 32+SCRATCH_SLOTS)

// ─── Value ───────────────────────────────────────────────────────────────────

#define VALUE_INT  0
#define VALUE_STR  1
#define VALUE_VOID 0xFF   // sentinel: built-in returns nothing

typedef struct {
    uint8_t type;   // VALUE_INT or VALUE_STR
    int32_t i;      // integer value, or string index when type == VALUE_STR
                    //   [0, MAX_STRINGS)    → string literal table
                    //   [SCRATCH_BASE, ...) → scratch pool slot
} Value;

// ─── VM state ─────────────────────────────────────────────────────────────────

static struct {
    // Loaded bytecode
    uint8_t  code[MAX_BYTECODE];
    uint16_t code_len;

    // String literal table
    char     strtab[MAX_STRINGS][MAX_STR_LEN + 1];
    uint8_t  str_count;

    // Entry points (0xFFFF = lifecycle function not defined in cart)
    uint16_t off_init;
    uint16_t off_update;
    uint16_t off_draw;
    uint16_t off_audio;

    // Global variable slots for lifecycle function parameters (0xFF = not bound)
    uint8_t  param_update_frame;
    uint8_t  param_update_input;
    uint8_t  param_draw_frame;
    uint8_t  param_draw_input;
    uint8_t  param_audio_t;

    // Live global variable table (core 0)
    Value    globals[MAX_VARS];

    // Audio shadow buffers — core 0 writes the inactive one then flips the index;
    // core 1 reads from the active one. Single-byte flip is naturally atomic on
    // RP2040 (Cortex-M0+); a DMB before the write ensures the memcpy is visible.
    Value    shadow[2][MAX_VARS];
    volatile uint8_t shadow_active;

    // Runtime string scratch pool — reset at the start of each exec() call.
    // Dynamic strings must not be held across lifecycle function boundaries (§6.3).
    char     scratch[SCRATCH_SLOTS][MAX_STR_LEN + 1];
    uint8_t  scratch_top;

    // Per-core evaluation stacks (core 0: init/update/draw; core 1: audio)
    Value    stack0[STACK_SIZE];
    Value    stack1[STACK_SIZE];

    // LCG random state
    uint32_t rng;

    bool     loaded;
    bool     exit_exec;  // set by loadcart to abort the current exec() call
} vm;

// ─── Binary reading helpers ───────────────────────────────────────────────────

static inline int32_t  ri32(const uint8_t *p) {
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8)
                   | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}
static inline int16_t  ri16(const uint8_t *p) {
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static inline uint16_t ru16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

// ─── String helpers ───────────────────────────────────────────────────────────

static const char *str_get(Value v) {
    if (v.type != VALUE_STR) return "";
    if (v.i >= SCRATCH_BASE) {
        int s = v.i - SCRATCH_BASE;
        return (s < SCRATCH_SLOTS) ? vm.scratch[s] : "";
    }
    return (v.i < vm.str_count) ? vm.strtab[v.i] : "";
}

static Value str_scratch(const char *s) {
    uint8_t slot = vm.scratch_top % SCRATCH_SLOTS;
    vm.scratch_top = slot + 1;
    strncpy(vm.scratch[slot], s, MAX_STR_LEN);
    vm.scratch[slot][MAX_STR_LEN] = '\0';
    return (Value){ VALUE_STR, SCRATCH_BASE + slot };
}

static Value exec(uint16_t entry, Value *globals, Value *stk);

// ─── Built-in dispatch ────────────────────────────────────────────────────────

static Value call_builtin(uint8_t id, Value *a) {
    switch ((BuiltinId)id) {

    // Input
    case BUILTIN_BTN:
        return (Value){ VALUE_INT, input_btn(a[0].i) };
    case BUILTIN_BTNP:
        return (Value){ VALUE_INT, input_btnp(a[0].i) };

    // Graphics
    case BUILTIN_CLS:
        display_cls(a[0].i);
        return (Value){ VALUE_VOID };
    case BUILTIN_PSET:
        display_pset(a[0].i, a[1].i, a[2].i);
        return (Value){ VALUE_VOID };
    case BUILTIN_RECTFILL:
        display_rectfill(a[0].i, a[1].i, a[2].i, a[3].i, a[4].i);
        return (Value){ VALUE_VOID };
    case BUILTIN_LINE:
        display_line(a[0].i, a[1].i, a[2].i, a[3].i, a[4].i);
        return (Value){ VALUE_VOID };
    case BUILTIN_PRINT: {
        char ibuf[16];
        const char *text;
        if (a[0].type == VALUE_INT) {
            snprintf(ibuf, sizeof(ibuf), "%d", (int)a[0].i);
            text = ibuf;
        } else {
            text = str_get(a[0]);
        }
        display_print(a[1].i, a[2].i, text, a[3].i);
        return (Value){ VALUE_VOID };
    }

    // Math
    case BUILTIN_ABS: {
        int32_t x = a[0].i;
        return (Value){ VALUE_INT, x < 0 ? -x : x };
    }
    case BUILTIN_MIN:
        return (Value){ VALUE_INT, a[0].i < a[1].i ? a[0].i : a[1].i };
    case BUILTIN_MAX:
        return (Value){ VALUE_INT, a[0].i > a[1].i ? a[0].i : a[1].i };
    case BUILTIN_CLAMP: {
        int32_t x = a[0].i, lo = a[1].i, hi = a[2].i;
        return (Value){ VALUE_INT, x < lo ? lo : x > hi ? hi : x };
    }
    case BUILTIN_SEED:
        vm.rng = (uint32_t)a[0].i;
        return (Value){ VALUE_VOID };
    case BUILTIN_RND: {
        int32_t n = a[0].i;
        if (n <= 0) return (Value){ VALUE_INT, 0 };
        vm.rng = vm.rng * 1664525u + 1013904223u;
        return (Value){ VALUE_INT, (int32_t)((vm.rng >> 16) % (uint32_t)n) };
    }

    // Strings
    case BUILTIN_LEN:
        return (Value){ VALUE_INT, (int32_t)strlen(str_get(a[0])) };
    case BUILTIN_CHAR: {
        const char *s = str_get(a[0]);
        int i = a[1].i, len = (int)strlen(s);
        if (i < 0 || i >= len) return str_scratch("");
        char buf[2] = { s[i], '\0' };
        return str_scratch(buf);
    }

    // Persistence — flash not yet implemented; stubs return safe defaults
    case BUILTIN_SAVE:
        return (Value){ VALUE_VOID };
    case BUILTIN_LOAD_SLOT:
        return (Value){ VALUE_INT, 0 };

    // Cart utilities
    case BUILTIN_CARTCOUNT:
        return (Value){ VALUE_INT, cart_count() };
    case BUILTIN_CARTMETA: {
        const char *val = cart_meta(a[0].i, str_get(a[1]));
        return str_scratch(val);
    }
    case BUILTIN_LOADCART: {
        uint32_t size;
        const uint8_t *bin = cart_get(a[0].i, &size);
        if (!bin || !vm_load(bin, size)) return (Value){ VALUE_INT, 0 };
        exec(vm.off_init, vm.globals, vm.stack0);
        vm.exit_exec = true;
        return (Value){ VALUE_VOID };
    }

    default:
        return (Value){ VALUE_VOID };
    }
}

// ─── Interpreter ─────────────────────────────────────────────────────────────
//
// exec() runs one lifecycle function body to completion.
// `globals` is either vm.globals (core 0) or vm.shadow[active] (core 1 audio).
// `stk`     is vm.stack0 (core 0) or vm.stack1 (core 1).
// Returns the top-of-stack on RET/end-of-code, or {VALUE_INT, 128} if empty.

static Value exec(uint16_t entry, Value *globals, Value *stk) {
    if (entry == 0xFFFF || !vm.loaded) return (Value){ VALUE_INT, 128 };

    uint16_t ip = entry;
    int      sp = 0;
    vm.scratch_top = 0;
    vm.exit_exec   = false;

#define PUSH(v)    do { if (sp < STACK_SIZE) stk[sp++] = (v); } while (0)
#define PUSH_I(n)  PUSH(((Value){ VALUE_INT, (int32_t)(n) }))
#define POP()      (sp > 0 ? stk[--sp] : (Value){ VALUE_INT, 0 })
#define PEEK()     (sp > 0 ? stk[sp-1]  : (Value){ VALUE_INT, 0 })
#define R8()       (vm.code[ip++])
#define R16()      (ip += 2, ri16(vm.code + ip - 2))
#define R32()      (ip += 4, ri32(vm.code + ip - 4))

    while (ip < vm.code_len) {
        switch ((Opcode)vm.code[ip++]) {

        // ── Literals ─────────────────────────────────────────────────────────
        case OP_PUSH_INT: PUSH_I(R32()); break;
        case OP_PUSH_STR: PUSH(((Value){ VALUE_STR, R8() })); break;

        // ── Variables ────────────────────────────────────────────────────────
        case OP_LOAD: {
            uint8_t s = R8();
            PUSH(s < MAX_VARS ? globals[s] : ((Value){ VALUE_INT, 0 }));
            break;
        }
        case OP_STORE: {
            uint8_t s = R8();
            Value v = POP();
            if (s < MAX_VARS) globals[s] = v;
            break;
        }

        // ── Arithmetic ───────────────────────────────────────────────────────
        case OP_ADD: {
            Value b = POP(), a = POP();
            if (a.type == VALUE_STR || b.type == VALUE_STR) {
                // String concatenation: result capped at MAX_STR_LEN (§2.8)
                char buf[MAX_STR_LEN + 1];
                const char *sa = str_get(a), *sb = str_get(b);
                int la = (int)strlen(sa);
                strncpy(buf, sa, MAX_STR_LEN);
                buf[MAX_STR_LEN] = '\0';
                if (la < MAX_STR_LEN) strncat(buf, sb, (size_t)(MAX_STR_LEN - la));
                PUSH(str_scratch(buf));
            } else {
                PUSH_I(a.i + b.i);
            }
            break;
        }
        case OP_SUB: { Value b = POP(), a = POP(); PUSH_I(a.i - b.i);              break; }
        case OP_MUL: { Value b = POP(), a = POP(); PUSH_I(a.i * b.i);              break; }
        case OP_DIV: { Value b = POP(), a = POP(); PUSH_I(b.i ? a.i / b.i : 0);   break; }
        case OP_MOD: { Value b = POP(), a = POP(); PUSH_I(b.i ? a.i % b.i : 0);   break; }
        case OP_NEG: { Value a = POP();             PUSH_I(-a.i);                   break; }

        // ── Bitwise ──────────────────────────────────────────────────────────
        case OP_BAND: { Value b = POP(), a = POP(); PUSH_I(a.i & b.i);          break; }
        case OP_BOR:  { Value b = POP(), a = POP(); PUSH_I(a.i | b.i);          break; }
        case OP_BXOR: { Value b = POP(), a = POP(); PUSH_I(a.i ^ b.i);          break; }
        case OP_SHL:  { Value b = POP(), a = POP(); PUSH_I(a.i << (b.i & 31));  break; }
        case OP_SHR:  { Value b = POP(), a = POP(); PUSH_I(a.i >> (b.i & 31));  break; }

        // ── Comparison ───────────────────────────────────────────────────────
        // == and != use content comparison for strings; others compare integers.
        case OP_EQ: {
            Value b = POP(), a = POP();
            int r = (a.type == VALUE_STR || b.type == VALUE_STR)
                    ? strcmp(str_get(a), str_get(b)) == 0
                    : a.i == b.i;
            PUSH_I(r);
            break;
        }
        case OP_NE: {
            Value b = POP(), a = POP();
            int r = (a.type == VALUE_STR || b.type == VALUE_STR)
                    ? strcmp(str_get(a), str_get(b)) != 0
                    : a.i != b.i;
            PUSH_I(r);
            break;
        }
        case OP_LT: { Value b = POP(), a = POP(); PUSH_I(a.i <  b.i); break; }
        case OP_LE: { Value b = POP(), a = POP(); PUSH_I(a.i <= b.i); break; }
        case OP_GT: { Value b = POP(), a = POP(); PUSH_I(a.i >  b.i); break; }
        case OP_GE: { Value b = POP(), a = POP(); PUSH_I(a.i >= b.i); break; }

        // ── Logical NOT ──────────────────────────────────────────────────────
        // Two in sequence (NOT NOT) normalise any nonzero value to 1 (§2.5 &&/||).
        case OP_NOT: { Value a = POP(); PUSH_I(!a.i); break; }

        // ── Stack ────────────────────────────────────────────────────────────
        case OP_POP: POP(); break;

        // ── Control flow ─────────────────────────────────────────────────────
        case OP_JUMP:       { int16_t off = R16(); ip = (uint16_t)(ip + off);                break; }
        case OP_JUMP_T:     { int16_t off = R16(); if (POP().i  != 0) ip = (uint16_t)(ip + off); break; }
        case OP_JUMP_F:     { int16_t off = R16(); if (POP().i  == 0) ip = (uint16_t)(ip + off); break; }
        case OP_PEEK_JUMP_T:{ int16_t off = R16(); if (PEEK().i != 0) ip = (uint16_t)(ip + off); break; }
        case OP_PEEK_JUMP_F:{ int16_t off = R16(); if (PEEK().i == 0) ip = (uint16_t)(ip + off); break; }

        // ── Built-in call ────────────────────────────────────────────────────
        // Args were pushed left-to-right; pop in reverse to restore order.
        case OP_CALL: {
            uint8_t id   = R8();
            uint8_t argc = R8();
            Value args[8] = {{ VALUE_INT, 0 }};
            for (int i = argc - 1; i >= 0; i--) args[i] = POP();
            Value result = call_builtin(id, args);
            if (result.type != VALUE_VOID) PUSH(result);
            if (vm.exit_exec) goto done;
            break;
        }

        case OP_RET: goto done;
        }
    }
done:
    return sp > 0 ? stk[sp - 1] : (Value){ VALUE_INT, 128 };

#undef PUSH
#undef PUSH_I
#undef POP
#undef PEEK
#undef R8
#undef R16
#undef R32
}

// ─── vm_load ─────────────────────────────────────────────────────────────────
//
// Binary format (.bdb) — all multi-byte integers little-endian:
//
//   [0]     4 B   magic 'B','D','B','N'
//   [4]     1 B   format version (1)
//   [5]     1 B   flags (reserved, must be 0)
//   [6]     2 B   metadata block length
//   [8]     N B   metadata block (ignored by runtime)
//   [8+N]   1 B   string count (0–32)
//           ?     string table: for each entry: [len: u8][chars: len bytes]
//           2 B   init_off   (0xFFFF = not defined)
//           2 B   update_off
//           1 B   update 'frame' param slot (0xFF = not bound)
//           1 B   update 'input' param slot
//           2 B   draw_off
//           1 B   draw 'frame' param slot
//           1 B   draw 'input' param slot
//           2 B   audio_off
//           1 B   audio 't' param slot
//           ?     bytecode stream

bool vm_load(const uint8_t *bin, uint32_t len) {
    if (len < 8) return false;
    if (bin[0] != 'B' || bin[1] != 'D' || bin[2] != 'B' || bin[3] != 'N') return false;
    if (bin[4] != 1) return false;

    const uint8_t *p   = bin + 6;
    const uint8_t *end = bin + len;

    // Skip metadata block
    if (p + 2 > end) return false;
    uint16_t meta_len = ru16(p);
    p += 2 + meta_len;
    if (p > end) return false;

    // String table
    if (p >= end) return false;
    uint8_t nstr = *p++;
    if (nstr > MAX_STRINGS) return false;
    vm.str_count = nstr;
    for (int i = 0; i < nstr; i++) {
        if (p >= end) return false;
        uint8_t slen = *p++;
        if (slen > MAX_STR_LEN || p + slen > end) return false;
        memcpy(vm.strtab[i], p, slen);
        vm.strtab[i][slen] = '\0';
        p += slen;
    }

    // Entry points + parameter slots (13 bytes total)
    if (p + 13 > end) return false;
    vm.off_init           = ru16(p); p += 2;
    vm.off_update         = ru16(p); p += 2;
    vm.param_update_frame = *p++;
    vm.param_update_input = *p++;
    vm.off_draw           = ru16(p); p += 2;
    vm.param_draw_frame   = *p++;
    vm.param_draw_input   = *p++;
    vm.off_audio          = ru16(p); p += 2;
    vm.param_audio_t      = *p++;

    // Bytecode
    uint32_t code_len = (uint32_t)(end - p);
    if (code_len > MAX_BYTECODE) return false;
    memcpy(vm.code, p, code_len);
    vm.code_len = (uint16_t)code_len;

    // Reset runtime state
    memset(vm.globals, 0, sizeof(vm.globals));
    memset(vm.shadow,  0, sizeof(vm.shadow));
    vm.shadow_active = 0;
    vm.scratch_top   = 0;
    vm.rng           = 12345;
    vm.loaded        = true;
    return true;
}

// ─── Lifecycle hooks ─────────────────────────────────────────────────────────

static void set_param(Value *globals, uint8_t slot, int32_t val) {
    if (slot < MAX_VARS) globals[slot] = (Value){ VALUE_INT, val };
}

void vm_call_init(void) {
    if (!vm.loaded) return;
    exec(vm.off_init, vm.globals, vm.stack0);
}

void vm_call_update(int frame, uint8_t input) {
    if (!vm.loaded) return;
    set_param(vm.globals, vm.param_update_frame, frame);
    set_param(vm.globals, vm.param_update_input,  input);
    exec(vm.off_update, vm.globals, vm.stack0);
}

void vm_call_draw(int frame, uint8_t input) {
    if (!vm.loaded) return;
    set_param(vm.globals, vm.param_draw_frame, frame);
    set_param(vm.globals, vm.param_draw_input,  input);
    exec(vm.off_draw, vm.globals, vm.stack0);
}

// Called from core 1 at 22 050 Hz (§5.3).
int vm_call_audio(int t) {
    if (!vm.loaded || vm.off_audio == 0xFFFF) return 128;
    uint8_t  active = vm.shadow_active;
    Value   *shadow = vm.shadow[active];
    set_param(shadow, vm.param_audio_t, t);
    Value result = exec(vm.off_audio, shadow, vm.stack1);
    int32_t s = result.i;
    return s < 0 ? 0 : s > 255 ? 255 : (int)s;
}

// Called from core 0 after vm_call_draw() (§5.3).
void vm_sync_audio_shadow(void) {
    uint8_t inactive = vm.shadow_active ^ 1;
    memcpy(vm.shadow[inactive], vm.globals, sizeof(vm.globals));
    // DMB: ensure memcpy is visible to core 1 before the index flip
    __asm volatile ("dmb" ::: "memory");
    vm.shadow_active = inactive;
}
