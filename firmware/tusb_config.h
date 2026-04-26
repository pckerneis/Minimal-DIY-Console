#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

// Controller
#define CFG_TUSB_MCU            OPT_MCU_RP2040
#define CFG_TUSB_RHPORT0_MODE   OPT_MODE_DEVICE

// Debug (0 = off)
#define CFG_TUSB_DEBUG          0

// Device stack
#define CFG_TUD_ENABLED         1

// Class drivers — disable everything you don't use
#define CFG_TUD_CDC             0
#define CFG_TUD_MSC             1
#define CFG_TUD_HID             0
#define CFG_TUD_MIDI            0
#define CFG_TUD_VENDOR          0

// MSC buffer
#define CFG_TUD_MSC_EP_BUFSIZE  512

#endif