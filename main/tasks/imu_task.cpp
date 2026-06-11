// ============================================================================
// imu_task.cpp - Le MPU6050 a 200 Hz e publica em g_imu_queue
// ============================================================================
// Affinity: Core 0
// Frequencia: 200 Hz (vTaskDelayUntil para ritmo preciso)
// Fila: 16 slots, drop-old quando cheia
//
// Auto-recovery: se IMU_ZERO_STREAK_LIMIT amostras consecutivas forem todas
// zero, presume que o chip resetou silenciosamente (glitch de VCC ou similar)
// e re-executa mpu6050_init() automaticamente. Loga no UART.
// ============================================================================

#include "imu_task.h"
#include "../imu_types.h"
#include "../sensors/mpu6050.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "pico/stdlib.h"

#include <stdio.h>

#define IMU_SAMPLE_RATE_HZ      200
#define IMU_PERIOD_MS           (1000 / IMU_SAMPLE_RATE_HZ)  // 5 ms
#define IMU_QUEUE_LENGTH        16
#define IMU_TASK_STACK          (configMINIMAL_STACK_SIZE * 2)
#define IMU_TASK_CORE_AFFINITY  (1u << 0)  // Core 0

// Se >= 200 amostras seguidas (1 segundo) tudo-zero, re-init o chip.
#define IMU_ZERO_STREAK_LIMIT   200

QueueHandle_t g_imu_queue = nullptr;
QueueHandle_t g_imu_ei_queue = nullptr;

static inline bool sample_is_all_zero(const imu_sample_t &s)
{
    return s.accel[0] == 0 && s.accel[1] == 0 && s.accel[2] == 0 &&
           s.gyro[0]  == 0 && s.gyro[1]  == 0 && s.gyro[2]  == 0;
}

static void imu_task_fn(void *p)
{
    (void)p;
    mpu6050_init();

    TickType_t last_wake = xTaskGetTickCount();
    uint32_t zero_streak = 0;
    uint32_t reinit_count = 0;
    uint32_t ei_decimate = 0;

    while (true) {
        imu_sample_t sample;
        mpu6050_read_raw(sample.accel, sample.gyro, &sample.temp_raw);
        sample.timestamp_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

        // Auto-recovery: chip retornando zeros constantes -> re-init.
        if (sample_is_all_zero(sample)) {
            zero_streak++;
            if (zero_streak >= IMU_ZERO_STREAK_LIMIT) {
                reinit_count++;
                printf("[imu] %lu amostras zero seguidas - re-init MPU (tentativa #%lu)\n",
                       (unsigned long)zero_streak, (unsigned long)reinit_count);
                mpu6050_init();
                zero_streak = 0;
                // Pequeno settle antes da primeira leitura nova.
                vTaskDelay(pdMS_TO_TICKS(20));
            }
        } else {
            zero_streak = 0;
        }

        // Drop-old: se a fila estiver cheia, descarta a mais antiga.
        if (xQueueSend(g_imu_queue, &sample, 0) != pdPASS) {
            imu_sample_t discarded;
            xQueueReceive(g_imu_queue, &discarded, 0);
            xQueueSend(g_imu_queue, &sample, 0);
        }

        // Fila da EI: decimada 2:1 (200Hz -> 100Hz). Drop-old tambem.
        if (g_imu_ei_queue != nullptr && (ei_decimate++ % 2) == 0) {
            if (xQueueSend(g_imu_ei_queue, &sample, 0) != pdPASS) {
                imu_sample_t discarded;
                xQueueReceive(g_imu_ei_queue, &discarded, 0);
                xQueueSend(g_imu_ei_queue, &sample, 0);
            }
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(IMU_PERIOD_MS));
    }
}

extern "C" BaseType_t imu_task_start(UBaseType_t priority)
{
    g_imu_queue = xQueueCreate(IMU_QUEUE_LENGTH, sizeof(imu_sample_t));
    if (g_imu_queue == nullptr) {
        return pdFAIL;
    }

    // Fila da EI - um pouco maior pra dar folga ao Core 1.
    g_imu_ei_queue = xQueueCreate(32, sizeof(imu_sample_t));
    if (g_imu_ei_queue == nullptr) {
        return pdFAIL;
    }

    TaskHandle_t handle = nullptr;
    BaseType_t rc = xTaskCreate(imu_task_fn, "imu", IMU_TASK_STACK,
                                nullptr, priority, &handle);
    if (rc != pdPASS) {
        return rc;
    }

    vTaskCoreAffinitySet(handle, IMU_TASK_CORE_AFFINITY);
    return pdPASS;
}
