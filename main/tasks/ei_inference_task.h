#ifndef EI_INFERENCE_TASK_H
#define EI_INFERENCE_TASK_H

#include "FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

// Cria a ei_inference_task fixada no Core 1.
// Consome g_imu_ei_queue (100Hz), monta janelas de 1s (100 amostras x 6 eixos),
// roda run_classifier() e, se a classe "shake_horizontal" passar do threshold
// (com cooldown), dispara um pulse no botao HOME via usb_hid_pulse_button().
//
// Isolada por affinity no Core 1 + prioridade baixa: nunca interfere no
// pipeline de motion (Core 0).
BaseType_t ei_inference_task_start(UBaseType_t priority);

#ifdef __cplusplus
}
#endif

#endif // EI_INFERENCE_TASK_H
