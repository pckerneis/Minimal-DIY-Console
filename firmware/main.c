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

static bool try_load_cart(const char *name) {
    // TODO: scan flash cart storage, find name, call vm_load()
    (void)name;
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
        display_print(2,  4, "USB Storage Mode");
        display_print(2, 14, fs_ok ? "FS: OK" : "FS: MISSING");
        display_print(2, 24, fs_ok ? "Connect USB cable" : "");
        display_print(2, 34, fs_ok ? "to your computer." : "");
        display_flush();

        tusb_init();
        for (;;) {
            tud_task();
            usb_msc_flush(); // flush write-behind cache; safe here, not in a callback
        }
        // never returns
    }

    // §5.1 step 2: attempt to load boot.bdbin
    if (!try_load_cart("boot.bdbin")) {
        // §5.1 step 3: show error, wait for button, fall back to built-in loader
        display_cls(0);
        display_print(2,  4, "No boot cart found.");
        display_print(2, 20, "Press any button");
        display_print(2, 30, "to continue.");
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

        frame++;

        sleep_until(frame_end);
    }
}
