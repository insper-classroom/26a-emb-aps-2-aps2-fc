#ifndef IMU_TASK_H
#define IMU_TASK_H

#include "FreeRTOS.h"
#include "queue.h"

#ifdef __cplusplus
extern "C" {
#endif

// Fila global publicada pela imu_task. Tipo dos elementos: imu_sample_t.
// Drop-old: se cheia, a amostra mais antiga e descartada e a nova entra.
// Inicializada por imu_task_start(). Consumida por motion_tx_task (200Hz).
extern QueueHandle_t g_imu_queue;

// Segunda fila, dedicada a Edge Impulse, decimada para 100Hz (casa com
// EI_CLASSIFIER_FREQUENCY). Consumida por ei_inference_task no Core 1.
// Existe separada pra que motion e EI nao "roubem" amostras um do outro
// (uma fila FreeRTOS entrega cada item a um unico consumidor).
extern QueueHandle_t g_imu_ei_queue;

// Cria a imu_task fixada no Core 0. Deve ser chamado antes de
// vTaskStartScheduler(). Retorna pdPASS em sucesso.
BaseType_t imu_task_start(UBaseType_t priority);

#ifdef __cplusplus
}
#endif

#endif // IMU_TASK_H
