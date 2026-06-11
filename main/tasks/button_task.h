#ifndef BUTTON_TASK_H
#define BUTTON_TASK_H

#include "FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

// Bit indices no HID gamepad buttons mask (combinam com usb_hid_set_buttons).
// DInput do Dolphin enxerga como Button N (0-indexado).
#define BTN_BIT_A      0
#define BTN_BIT_B      1
#define BTN_BIT_LEFT   2   // D-Pad Esquerda (TSW + encoder CCW)
#define BTN_BIT_RIGHT  3   // D-Pad Direita  (TSW + encoder CW)
#define BTN_BIT_HOME   4   // Acionado pela EI (gesto shake sustentado)
#define BTN_BIT_UP     5   // D-Pad Cima (TSW)
#define BTN_BIT_DOWN   6   // D-Pad Baixo (TSW)

// Cria a button_task fixada no Core 0 (lendo GPIO a ~100 Hz, debounce
// assimetrico: 30ms press / 150ms release).
BaseType_t button_task_start(UBaseType_t priority);

#ifdef __cplusplus
}
#endif

#endif // BUTTON_TASK_H
