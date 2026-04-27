#include "cart.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

// FAT12 layout — must stay in sync with usb_msc.c and tools/gen_fs.py.
#define DISK_BASE       ((const uint8_t *)(0x10000000u + 512u * 1024u))
#define SEC             512u
#define SPC             8u
#define FAT_SECS        3u
#define ROOT_MAX        512u
#define ROOT_SEC        (1u + 2u * FAT_SECS)
#define DATA_SEC        (ROOT_SEC + ROOT_MAX * 32u / SEC)

// ── FAT12 helpers ─────────────────────────────────────────────────────────────

static uint16_t fat12_entry(uint16_t clus) {
    const uint8_t *fat = DISK_BASE + SEC;
    uint32_t off = (uint32_t)clus + clus / 2;
    uint16_t v   = fat[off] | ((uint16_t)fat[off + 1] << 8);
    return (clus & 1) ? (v >> 4) : (v & 0x0FFF);
}

static const uint8_t *clus_to_ptr(uint16_t clus) {
    return DISK_BASE + DATA_SEC * SEC + (uint32_t)(clus - 2) * SPC * SEC;
}

static bool chain_contiguous(uint16_t first) {
    uint16_t c = first, prev = 0;
    while (c >= 2 && c < 0xFF8) {
        if (prev && c != (uint16_t)(prev + 1)) return false;
        prev = c; c = fat12_entry(c);
    }
    return true;
}

// ── Directory scanner ─────────────────────────────────────────────────────────
//
// Walks the FAT12 root directory and calls the visitor for each valid .bdb
// file. Returns the total count of valid carts found.

typedef void (*cart_visit_fn)(int idx, const uint8_t *data, uint32_t size,
                              void *ctx);

static const int LFN_OFF[13] = {1,3,5,7,9, 14,16,18,20,22,24, 28,30};

static int scan(cart_visit_fn visit, void *ctx) {
    const uint8_t *root = DISK_BASE + ROOT_SEC * SEC;
    char lfn[32] = {0}; int lfn_len = 0; bool has_lfn = false;
    int count = 0;

    for (int i = 0; i < (int)ROOT_MAX; i++) {
        const uint8_t *e = root + i * 32;
        if (e[0] == 0x00) break;
        if (e[0] == 0xE5) { has_lfn = false; lfn_len = 0; continue; }

        uint8_t attr = e[11];

        if (attr == 0x0F) {
            int base = ((e[0] & 0x1F) - 1) * 13;
            for (int k = 0; k < 13; k++) {
                uint8_t lo = e[LFN_OFF[k]], hi = e[LFN_OFF[k] + 1];
                if ((lo == 0 && hi == 0) || (lo == 0xFF && hi == 0xFF)) break;
                int pos = base + k;
                if (pos < 31) {
                    char c = (char)lo;
                    lfn[pos] = (c >= 'A' && c <= 'Z') ? c + 32 : c;
                    if (pos >= lfn_len) lfn_len = pos + 1;
                }
            }
            lfn[lfn_len] = '\0';
            has_lfn = true;
            continue;
        }

        bool cur_has_lfn = has_lfn;
        int  cur_lfn_len = lfn_len;
        has_lfn = false; lfn_len = 0;

        if (attr & 0x18) continue; // volume label or subdirectory

        // Identify .bdb by 8.3 extension (always "BDB" regardless of LFN).
        if (e[8] != 'B' || e[9] != 'D' || e[10] != 'B') {
            // Also accept lowercase in LFN tail for robustness.
            if (!cur_has_lfn || cur_lfn_len < 4) continue;
            const char *t = lfn + cur_lfn_len - 4;
            if (t[0] != '.' || t[1] != 'b' || t[2] != 'd' || t[3] != 'b') continue;
        }

        uint16_t clus = (uint16_t)(e[26] | (e[27] << 8));
        uint32_t size = (uint32_t)(e[28] | (e[29]<<8) | (e[30]<<16) | (e[31]<<24));
        if (clus < 2 || size == 0 || !chain_contiguous(clus)) continue;

        const uint8_t *data = clus_to_ptr(clus);
        if (size < 8 || data[0]!='B' || data[1]!='D' || data[2]!='B'
                     || data[3]!='N' || data[4] != 1) continue;

        if (visit) visit(count, data, size, ctx);
        count++;
    }
    return count;
}

// ── cart_count ────────────────────────────────────────────────────────────────

int cart_count(void) {
    return scan(NULL, NULL);
}

// ── cart_get ──────────────────────────────────────────────────────────────────

typedef struct { int want; const uint8_t *data; uint32_t size; } GetCtx;

static void get_visit(int idx, const uint8_t *data, uint32_t size, void *ctx) {
    GetCtx *g = ctx;
    if (idx == g->want) { g->data = data; g->size = size; }
}

const uint8_t *cart_get(int index, uint32_t *out_size) {
    GetCtx g = { .want = index, .data = NULL, .size = 0 };
    int total = scan(get_visit, &g);
    if (index < 0 || index >= total || !g.data) return NULL;
    if (out_size) *out_size = g.size;
    return g.data;
}

// ── cart_meta ─────────────────────────────────────────────────────────────────

static char meta_buf[128];

const char *cart_meta(int index, const char *field) {
    uint32_t size;
    const uint8_t *data = cart_get(index, &size);
    if (!data) return "";

    uint16_t meta_len = (uint16_t)(data[6] | (data[7] << 8));
    if ((uint32_t)(8 + meta_len) > size) return "";

    const char *meta = (const char *)(data + 8);

    // Build the search prefix "@<field> "
    char prefix[72] = "@";
    int  plen = 1;
    for (const char *f = field; *f && plen < 68; ) prefix[plen++] = *f++;
    prefix[plen++] = ' ';
    prefix[plen]   = '\0';

    for (int i = 0; i < (int)meta_len; ) {
        if (i + plen <= (int)meta_len
                && memcmp(meta + i, prefix, (size_t)plen) == 0) {
            int start = i + plen, end = start;
            while (end < (int)meta_len && meta[end] != '\n' && meta[end] != '\r')
                end++;
            int vlen = end - start;
            if (vlen > (int)sizeof(meta_buf) - 1) vlen = sizeof(meta_buf) - 1;
            memcpy(meta_buf, meta + start, (size_t)vlen);
            meta_buf[vlen] = '\0';
            return meta_buf;
        }
        while (i < (int)meta_len && meta[i] != '\n') i++;
        if (i < (int)meta_len) i++;
    }
    return "";
}
