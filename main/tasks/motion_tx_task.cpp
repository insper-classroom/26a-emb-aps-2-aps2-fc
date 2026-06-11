// ============================================================================
// motion_tx_task.cpp - Drena g_imu_queue e envia CSV pelo CDC USB
// ============================================================================
// Affinity: Core 0
// Taxa de saida: ~100 Hz (decimacao 2:1 da fonte 200 Hz do IMU).
// Formato:  "ax,ay,az,gx,gy,gz\n"   (inteiros raw separados por virgula).
//
// O mesmo formato e aceito pelo edge-impulse-data-forwarder (na coleta de
// dataset, Fase 5.1) e pelo bridge.py (em runtime, Fase 3 PC-side).
// ============================================================================

#include "motion_tx_task.h"
#include "usb_task.h"
#include "../imu_types.h"
#include "../tasks/imu_task.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include <stdio.h>
#include <string.h>

#define MOTION_TX_STACK         (configMINIMAL_STACK_SIZE * 2)
#define MOTION_TX_CORE_AFFINITY (1u << 0)  // Core 0

// Decimacao: pega 1 amostra a cada N. IMU=200Hz, N=2 -> saida 100Hz.
#define DECIMATE_N              2

static void motion_tx_task_fn(void *p)
{
    (void)p;
    char line[80];
    uint32_t skip = 0;
    uint32_t sent_count = 0;
    TickType_t last_log = xTaskGetTickCount();

    while (true) {
        imu_sample_t s;
        if (xQueueReceive(g_imu_queue, &s, portMAX_DELAY) != pdPASS) continue;

        if ((skip++ % DECIMATE_N) != 0) continue;

        int n = snprintf(line, sizeof(line),
                         "%d,%d,%d,%d,%d,%d\n",
                         s.accel[0], s.accel[1], s.accel[2],
                         s.gyro[0],  s.gyro[1],  s.gyro[2]);
        if (n > 0 && (size_t)n < sizeof(line)) {
            usb_cdc_write(line, (uint32_t)n);
            sent_count++;
        }

        // Diagnostico a cada 1s: confirma que a task esta viva e produzindo,
        // E que o CDC esta efetivamente transmitindo (nao so bufferizando).
        if ((xTaskGetTickCount() - last_log) >= pdMS_TO_TICKS(1000)) {
            uint32_t ok = 0, drop = 0, avail = 0;
            usb_cdc_stats(&ok, &drop, &avail);
            printf("[mtx] tx=%lu cdc_ok=%lu cdc_drop=%lu cdc_avail=%lu\n",
                   (unsigned long)sent_count,
                   (unsigned long)ok, (unsigned long)drop,
                   (unsigned long)avail);
            sent_count = 0;
            last_log = xTaskGetTickCount();
        }
    }
}

extern "C" BaseType_t motion_tx_task_start(UBaseType_t priority)
{
    TaskHandle_t handle = nullptr;
    BaseType_t rc = xTaskCreate(motion_tx_task_fn, "mtx",
                                MOTION_TX_STACK, nullptr, priority, &handle);
    if (rc != pdPASS) return rc;
    vTaskCoreAffinitySet(handle, MOTION_TX_CORE_AFFINITY);
    return pdPASS;
}
