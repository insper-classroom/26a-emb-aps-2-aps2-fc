// ============================================================================
// tusb_config.h - Configuracao TinyUSB para composite HID Gamepad + CDC
// ============================================================================

#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

// --- Common config -----------------------------------------------------------
#ifndef CFG_TUSB_MCU
#define CFG_TUSB_MCU            OPT_MCU_RP2040  // mesmo OPT_MCU para RP2350
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS             OPT_OS_FREERTOS
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG          0
#endif

// Root hub port: usamos rhport 0 em modo device.
#define CFG_TUSB_RHPORT0_MODE   OPT_MODE_DEVICE
#define BOARD_TUD_RHPORT        0
#define TUD_OPT_RHPORT          0

#define CFG_TUD_ENABLED         1
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN      __attribute__ ((aligned(4)))

#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE  64
#endif

// --- Class drivers habilitados ----------------------------------------------
#define CFG_TUD_CDC             1
#define CFG_TUD_HID             1
#define CFG_TUD_MSC             0
#define CFG_TUD_MIDI            0
#define CFG_TUD_VENDOR          0

// --- HID buffer size --------------------------------------------------------
#define CFG_TUD_HID_EP_BUFSIZE  16

// --- CDC: FIFO sizes --------------------------------------------------------
#define CFG_TUD_CDC_RX_BUFSIZE  256
#define CFG_TUD_CDC_TX_BUFSIZE  512
#define CFG_TUD_CDC_EP_BUFSIZE  64

#ifdef __cplusplus
}
#endif

#endif // TUSB_CONFIG_H
