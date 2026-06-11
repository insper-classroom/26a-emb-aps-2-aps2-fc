#ifndef FEEDBACK_TASK_H
#define FEEDBACK_TASK_H

#include "FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

// Cria a feedback_task (qualquer core, prio baixa).
// Sequencia de boot:
//   1. Beep 440 Hz 100ms + pulso vibra 150ms (=> "tem energia, controle vivo")
//   2. Aguarda tud_mounted() (PC reconheceu o USB)
//   3. LED de status ACENDE + beep 880 Hz 80ms (=> "pronto pra jogar")
// Depois disso, fica suspensa - LED permanece aceso.
BaseType_t feedback_task_start(UBaseType_t priority);

#ifdef __cplusplus
}
#endif

#endif // FEEDBACK_TASK_H
