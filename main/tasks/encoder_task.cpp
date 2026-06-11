// ============================================================================
// encoder_task.cpp - Drena g_encoder_queue e converte em pulses HID LEFT/RIGHT
// ============================================================================
// Affinity: Core 0
// Comportamento:
//   - Bloqueia em xQueueReceive(g_encoder_queue)
//   - Cada item +1 (CW) -> usb_hid_pulse_button(BTN_BIT_RIGHT, 50ms)
//   - Cada item -1 (CCW) -> usb_hid_pulse_button(BTN_BIT_LEFT,  50ms)
//
// O encoder se "faz passar" pelos botoes direcionais LEFT/RIGHT do TSW
// (mesmos bits HID), entao Dolphin nao precisa de mapeamento separado.
// ============================================================================

#include "encoder_task.h"
#include "button_task.h"      // BTN_BIT_LEFT / BTN_BIT_RIGHT
#include "usb_task.h"         // usb_hid_pulse_button
#include "../encoder/encoder.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include <stdio.h>

#define ENC_TASK_STACK          (configMINIMAL_STACK_SIZE * 2)
#define ENC_TASK_CORE_AFFINITY  (1u << 0)   // Core 0
#define ENC_PULSE_MS            50          // duracao do "press" virtual

static void encoder_task_fn(void *p)
{
    (void)p;
    // Inicializa o encoder AQUI (dentro da task) em vez de em main(). Habilitar
    // o GPIO IRQ antes do scheduler causa IRQ storm que trava outras tasks
    // (botoes ficam sem responder quando GP2 flutua).
    encoder_init();
    printf("[enc] init OK (deferred to task context)\n");

    while (true) {
        int8_t dir = 0;
        if (xQueueReceive(g_encoder_queue, &dir, portMAX_DELAY) != pdPASS) continue;
        if (dir > 0) {
            usb_hid_pulse_button(BTN_BIT_RIGHT, ENC_PULSE_MS);
            printf("[enc] CW  -> RIGHT\n");
        } else if (dir < 0) {
            usb_hid_pulse_button(BTN_BIT_LEFT, ENC_PULSE_MS);
            printf("[enc] CCW -> LEFT\n");
        }
    }
}

extern "C" BaseType_t encoder_task_start(UBaseType_t priority)
{
    TaskHandle_t handle = nullptr;
    BaseType_t rc = xTaskCreate(encoder_task_fn, "enc",
                                ENC_TASK_STACK, nullptr, priority, &handle);
    if (rc != pdPASS) return rc;
    vTaskCoreAffinitySet(handle, ENC_TASK_CORE_AFFINITY);
    return pdPASS;
}
