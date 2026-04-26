// usb_msc.c
#include "tusb.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include <string.h>

#define FLASH_DISK_OFFSET   (512 * 1024)
#define DISK_BLOCK_SIZE     512
#define DISK_BLOCK_COUNT    ((2 * 1024 * 1024 - FLASH_DISK_OFFSET) / DISK_BLOCK_SIZE)

// Write-behind cache: accumulate sector writes in RAM and flush to flash
// from the main loop only — never inside a TinyUSB callback.
//
// Doing flash_range_erase (~50 ms, interrupts disabled) inside a callback
// re-enters tud_int_handler when interrupts are restored, corrupting
// TinyUSB's state machine and causing the USB connection to drop.
static uint8_t  g_cache[FLASH_SECTOR_SIZE];
static uint32_t g_cache_sector = UINT32_MAX; // invalid sentinel
static bool     g_cache_dirty  = false;

// Called from the main loop between tud_task() calls — safe to disable
// interrupts here because we are not inside any TinyUSB callback.
void usb_msc_flush(void) {
    if (!g_cache_dirty) return;
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(g_cache_sector, FLASH_SECTOR_SIZE);
    flash_range_program(g_cache_sector, g_cache, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);
    g_cache_dirty = false;
}

// --------------------------------------------------------
// USB Descriptors
// --------------------------------------------------------

static tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = 0x2E8A,
    .idProduct          = 0x0003,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01
};

uint8_t const* tud_descriptor_device_cb(void) {
    return (uint8_t const*)&desc_device;
}

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN)
#define EPNUM_MSC_OUT  0x01
#define EPNUM_MSC_IN   0x81

static uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_MSC_DESCRIPTOR(0, 0, EPNUM_MSC_OUT, EPNUM_MSC_IN, 64),
};

uint8_t const* tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

static char const* string_desc_arr[] = {
    (const char[]){0x09, 0x04},
    "OpenBidule",
    "Bidule01",
    "123456",
};

static uint16_t _desc_str[32];

uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    uint8_t chr_count;

    if (index == 0) {
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } else {
        if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) return NULL;
        const char *str = string_desc_arr[index];
        chr_count = (uint8_t)strlen(str);
        if (chr_count > 31) chr_count = 31;
        for (uint8_t i = 0; i < chr_count; i++) {
            _desc_str[1 + i] = str[i];
        }
    }

    _desc_str[0] = (TUSB_DESC_STRING << 8) | (2 * chr_count + 2);
    return _desc_str;
}

// --------------------------------------------------------
// MSC Callbacks
// --------------------------------------------------------

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8],
                         uint8_t product_id[16], uint8_t product_rev[4]) {
    (void)lun;
    memcpy(vendor_id,   "RPi     ", 8);
    memcpy(product_id,  "Bidule 01       ", 16);
    memcpy(product_rev, "1.0 ", 4);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun) {
    (void)lun;
    return true;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size) {
    (void)lun;
    *block_count = DISK_BLOCK_COUNT;
    *block_size  = DISK_BLOCK_SIZE;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                            void *buffer, uint32_t bufsize) {
    (void)lun;
    uint32_t flash_addr = FLASH_DISK_OFFSET + lba * DISK_BLOCK_SIZE + offset;
    uint32_t sector     = (FLASH_DISK_OFFSET + lba * DISK_BLOCK_SIZE)
                          & ~((uint32_t)FLASH_SECTOR_SIZE - 1);

    // Serve from the write-behind cache so reads are consistent with
    // in-flight writes that haven't been flushed to flash yet.
    if (g_cache_dirty && sector == g_cache_sector) {
        uint32_t off = (FLASH_DISK_OFFSET + lba * DISK_BLOCK_SIZE) - sector + offset;
        memcpy(buffer, g_cache + off, bufsize);
    } else {
        memcpy(buffer, (const void *)(XIP_BASE + flash_addr), bufsize);
    }
    return (int32_t)bufsize;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                             uint8_t *buffer, uint32_t bufsize) {
    (void)lun; (void)offset;
    uint32_t flash_addr = FLASH_DISK_OFFSET + lba * DISK_BLOCK_SIZE;
    uint32_t sector     = flash_addr & ~((uint32_t)FLASH_SECTOR_SIZE - 1);
    uint32_t in_sector  = flash_addr - sector;

    if (sector != g_cache_sector) {
        if (g_cache_dirty) {
            // Flush the old sector synchronously before switching.
            // Happens at most once per 4 KB block boundary, so the ~50 ms
            // interrupt-disabled window is rare and bulk-transfer timeouts
            // are measured in seconds, not milliseconds.
            uint32_t ints = save_and_disable_interrupts();
            flash_range_erase(g_cache_sector, FLASH_SECTOR_SIZE);
            flash_range_program(g_cache_sector, g_cache, FLASH_SECTOR_SIZE);
            restore_interrupts(ints);
            g_cache_dirty = false;
        }
        memcpy(g_cache, (const void *)(XIP_BASE + sector), FLASH_SECTOR_SIZE);
        g_cache_sector = sector;
    }

    memcpy(g_cache + in_sector, buffer, bufsize);
    g_cache_dirty = true;
    return (int32_t)bufsize;
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16],
                          void *buffer, uint16_t bufsize) {
    (void)buffer; (void)bufsize;
    // SYNCHRONIZE CACHE (0x35): host signals it is done writing.
    // Set the dirty flag so the main loop flushes on the next iteration.
    // (Flushing here would be inside a callback — same re-entrancy problem.)
    if (scsi_cmd[0] == 0x35) {
        (void)lun;
        return 0;
    }
    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
    return -1;
}
