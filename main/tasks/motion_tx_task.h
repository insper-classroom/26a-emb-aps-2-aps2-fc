#ifndef MOTION_TX_TASK_H
#define MOTION_TX_TASK_H

#include "FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

// Cria a motion_tx_task (Core 0). Consome g_imu_queue e escreve no CDC USB
// no formato "ax,ay,az,gx,gy,gz\n" (decimando para ~100 Hz). Compativel
// com edge-impulse-data-forwarder (durante coleta de dataset) e com
// bridge.py (em runtime para Dolphin via DSU).
BaseType_t motion_tx_task_start(UBaseType_t priority);

#ifdef __cplusplus
}
#endif

#endif // MOTION_TX_TASK_H
