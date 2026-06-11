#ifndef ENCODER_H
#define ENCODER_H

#include "FreeRTOS.h"
#include "queue.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Fila com eventos do encoder. Cada item e um int8_t:
//   +1 = CW  (1 detent pra direita)
//   -1 = CCW (1 detent pra esquerda)
// Postada pela ISR encoder_gpio_irq; consumida por encoder_task.
extern QueueHandle_t g_encoder_queue;

// Configura GP2 (A) e GP3 (B) como entradas com pull-up e instala o
// GPIO IRQ handler em ambas as bordas do A. Chame ANTES de iniciar
// encoder_task (ou em qualquer momento - a fila e criada aqui).
void encoder_init(void);

#ifdef __cplusplus
}
#endif

#endif // ENCODER_H
