#ifndef ENCODER_TASK_H
#define ENCODER_TASK_H

#include "FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

// Cria a encoder_task fixada no Core 0. Consome g_encoder_queue e dispara
// pulses HID curtos em BTN_BIT_LEFT/BTN_BIT_RIGHT (~50ms) via
// usb_hid_pulse_button. Cada detent do encoder = 1 evento de seta.
BaseType_t encoder_task_start(UBaseType_t priority);

#ifdef __cplusplus
}
#endif

#endif // ENCODER_TASK_H
