#include "pico/stdlib.h"
#include "display.h"
#include "input.h"
#include "audio.h"
#include "runtime/vm.h"
#include "tusb.h"

#if defined(CYW43_WL_GPIO_LED_PIN)
#include "pico/cyw43_arch.h"
#define LED_INIT()  cyw43_arch_init()
#define LED_ON()    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1)
#define LED_OFF()   cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0)
#else
#define LED_INIT()  do { gpio_init(PICO_DEFAULT_LED_PIN); gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT); } while(0)
#define LED_ON()    gpio_put(PICO_DEFAULT_LED_PIN, 1)
#define LED_OFF()   gpio_put(PICO_DEFAULT_LED_PIN, 0)
#endif

#define TARGET_FPS      30
#define FRAME_PERIOD_MS (1000 / TARGET_FPS)

// --- Cart loading ------------------------------------------------------------
//
// FAT12 layout — must stay in sync with FLASH_DISK_OFFSET in usb_msc.c
// and the constants in tools/gen_fs.py.
#define CART_DISK_BASE    (0x10000000u + 512u * 1024u)  // XIP_BASE + FLASH_DISK_OFFSET
#define CART_SEC          512u
#define CART_SPC          8u      // sectors per cluster
#define CART_FAT_SECS     3u
#define CART_ROOT_ENTRIES 512u
#define CART_ROOT_SEC     (1u + 2u * CART_FAT_SECS)
#define CART_DATA_SEC     (CART_ROOT_SEC + CART_ROOT_ENTRIES * 32u / CART_SEC)

static uint16_t fat12_entry(const uint8_t *fat, uint16_t clus) {
    uint32_t off = (uint32_t)clus + clus / 2;
    uint16_t v   = fat[off] | ((uint16_t)fat[off + 1] << 8);
    return (clus & 1) ? (v >> 4) : (v & 0x0FFF);
}

static bool try_load_cart(const char *name) {
    const uint8_t *disk = (const uint8_t *)CART_DISK_BASE;
    const uint8_t *fat  = disk + CART_SEC;
    const uint8_t *root = disk + CART_ROOT_SEC * CART_SEC;

    // Lowercase the search name for case-insensitive comparison.
    char target[32]; int tlen = 0;
    for (int i = 0; name[i] && tlen < 31; i++) {
        char c = name[i];
        target[tlen++] = (c >= 'A' && c <= 'Z') ? c + 32 : c;
    }
    target[tlen] = '\0';

    // Byte offsets of the 13 UTF-16LE characters inside a FAT LFN entry.
    static const int LFN_OFF[13] = {1,3,5,7,9, 14,16,18,20,22,24, 28,30};

    char lfn[32] = {0}; int lfn_len = 0; bool has_lfn = false;

    for (int i = 0; i < (int)CART_ROOT_ENTRIES; i++) {
        const uint8_t *e = root + i * 32;

        if (e[0] == 0x00) break;                         // end of directory
        if (e[0] == 0xE5) { has_lfn = false; lfn_len = 0; continue; } // deleted

        uint8_t attr = e[11];

        if (attr == 0x0F) {
            // Long File Name entry: accumulate chars ordered by sequence number.
            // "boot.bdb" (10 chars) fits in one LFN entry (seq 0x41).
            int base = ((e[0] & 0x1F) - 1) * 13;
            for (int k = 0; k < 13; k++) {
                uint8_t lo = e[LFN_OFF[k]], hi = e[LFN_OFF[k] + 1];
                if (lo == 0x00 && hi == 0x00) break;  // null terminator
                if (lo == 0xFF && hi == 0xFF) break;  // padding
                int pos = base + k;
                if (pos < 31) {
                    char c = (char)lo;
                    lfn[pos]   = (c >= 'A' && c <= 'Z') ? c + 32 : c;
                    if (pos >= lfn_len) lfn_len = pos + 1;
                }
            }
            lfn[lfn_len] = '\0';
            has_lfn = true;
            continue;
        }

        if (attr & 0x18) { has_lfn = false; lfn_len = 0; continue; } // dir/label

        // Compare: prefer LFN, fall back to 8.3 short name.
        bool match = false;
        if (has_lfn && lfn_len > 0) {
            match = (strcmp(lfn, target) == 0);
        } else {
            char s83[13]; int si = 0;
            for (int k = 0; k < 8 && e[k] != ' '; k++) {
                char c = e[k]; s83[si++] = (c >= 'A' && c <= 'Z') ? c + 32 : c;
            }
            if (e[8] != ' ') {
                s83[si++] = '.';
                for (int k = 8; k < 11 && e[k] != ' '; k++) {
                    char c = e[k]; s83[si++] = (c >= 'A' && c <= 'Z') ? c + 32 : c;
                }
            }
            s83[si] = '\0';
            match = (strcmp(s83, target) == 0);
        }
        has_lfn = false; lfn_len = 0;
        if (!match) continue;

        uint16_t first_clus = (uint16_t)(e[26] | (e[27] << 8));
        uint32_t size       = (uint32_t)(e[28] | (e[29] << 8) | (e[30] << 16) | (e[31] << 24));
        if (first_clus < 2 || size == 0) return false;

        // Verify the cluster chain is contiguous so we can hand an XIP pointer
        // directly to vm_load() without copying. Files on a freshly formatted
        // drive are always contiguous; fragmented files are not supported.
        uint16_t c = first_clus, prev = 0;
        while (c >= 2 && c < 0xFF8) {
            if (prev > 0 && c != (uint16_t)(prev + 1)) return false;
            prev = c;
            c = fat12_entry(fat, c);
        }

        uint32_t off = CART_DATA_SEC * CART_SEC
                       + (uint32_t)(first_clus - 2) * CART_SPC * CART_SEC;
        return vm_load(disk + off, size);
    }
    return false;
}

static void wait_any_button(void) {
    for (;;) {
        input_update();
        for (int i = 0; i < BTN_COUNT; i++)
            if (input_btnp(i)) return;
        sleep_ms(16);
    }
}

static bool is_any_button_pressed() {
    input_update();

    for (int i = 0; i < BTN_COUNT; i++)
        if (input_btnp(i)) return true;

    return false;
}

// --- Boot sequence (§5.1) ----------------------------------------------------

int main(void) {
    stdio_init_all();
    LED_INIT();
    LED_ON();

    // §5.1 step 1: initialise subsystems
    display_init();
    input_init();
    audio_init();

    // USB mode: if a button is held on boot, enter USB mass storage mode
    void usb_msc_flush(void);
    if (is_any_button_pressed()) {
        // Check that the pre-baked filesystem reached flash.
        // Expected: 0xEB at byte 0, 0x55 0xAA at bytes 510-511.
        const volatile uint8_t *fs = (const volatile uint8_t *)0x10080000u;
        bool fs_ok = (fs[0] == 0xEB && fs[510] == 0x55 && fs[511] == 0xAA);

        display_cls(0);
        display_print(2,  4, "USB Storage Mode", 1);
        display_print(2, 14, fs_ok ? "FS: OK" : "FS: MISSING", 1);
        display_print(2, 24, fs_ok ? "Connect USB cable" : "", 1);
        display_print(2, 34, fs_ok ? "to your computer." : "", 1);
        display_flush();

        tusb_init();
        for (;;) {
            usb_msc_flush(); // flush before tud_task so cache is clean on next write
            tud_task();
        }
        // never returns
    }

    // §5.1 step 2: attempt to load boot.bdb
    if (!try_load_cart("boot.bdb")) {
        // §5.1 step 3: show error, wait for button, fall back to built-in loader
        display_cls(0);
        display_print(2,  4, "No boot cart found.", 1);
        display_print(2, 20, "Press any button", 1);
        display_print(2, 30, "to continue.", 1);
        display_flush();
        wait_any_button();

        // TODO: load built-in cart selector
    }

    // §5.1 steps 4-5: init global table, call init()
    audio_set_callback(vm_call_audio);
    vm_call_init();

    // §5.2 main loop
    int frame = 0;
    for (;;) {
        absolute_time_t frame_end = make_timeout_time_ms(FRAME_PERIOD_MS);

        // poll input
        input_update();
        uint8_t mask = input_mask();

        // run cart
        vm_call_update(frame, mask);
        vm_call_draw(frame, mask);
        display_flush();

        // update audio shadow buffer after draw (§5.3)
        vm_sync_audio_shadow();

        if (vm_cart_switched()) frame = 0; else frame++;

        sleep_until(frame_end);
    }
}
