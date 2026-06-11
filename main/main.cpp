// ============================================================================
// main.cpp - Controle Pico 2 para Wii Bowling no Dolphin
// ============================================================================
// Fase 2: TinyUSB HID Gamepad + CDC composite + button_task + usb_task.
// IMU continua publicando em g_imu_queue; a imu_debug_task imprime via UART
// (motion_tx_task real entra na Fase 3).
// ============================================================================

#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"

#include "pins.h"
#include "imu_types.h"
#include "tasks/imu_task.h"
#include "tasks/usb_task.h"
#include "tasks/button_task.h"
#include "tasks/motion_tx_task.h"
#include "tasks/feedback_task.h"
#include "tasks/ei_inference_task.h"
#include "tasks/encoder_task.h"

// ----------------------------------------------------------------------------
// Prioridades
// ----------------------------------------------------------------------------
#define PRIO_USB        (configMAX_PRIORITIES - 2)
#define PRIO_IMU        (configMAX_PRIORITIES - 3)
#define PRIO_MOTION_TX  (configMAX_PRIORITIES - 3)
#define PRIO_BUTTON     (tskIDLE_PRIORITY + 3)
#define PRIO_FEEDBACK   (tskIDLE_PRIORITY + 1)
#define PRIO_DEBUG      (tskIDLE_PRIORITY + 1)
#define PRIO_EI_CAPTURE (tskIDLE_PRIORITY + 2)
#define PRIO_EI_INFER   (tskIDLE_PRIORITY + 2)

// ----------------------------------------------------------------------------
// Debug UART init (UART0 em GP0/GP1)
// ----------------------------------------------------------------------------
static void debug_uart_init(void)
{
    uart_init(uart0, DEBUG_UART_BAUD);
    gpio_set_function(DEBUG_UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(DEBUG_UART_RX_PIN, GPIO_FUNC_UART);
    uart_set_hw_flow(uart0, false, false);
    uart_set_format(uart0, 8, 1, UART_PARITY_NONE);
}

// ----------------------------------------------------------------------------
// Heartbeat: LED onboard pisca a 1 Hz.
// ----------------------------------------------------------------------------
static void heartbeat_task(void *p)
{
    (void)p;
    gpio_init(ONBOARD_LED_PIN);
    gpio_set_dir(ONBOARD_LED_PIN, GPIO_OUT);

    TickType_t last = xTaskGetTickCount();
    bool on = false;
    while (true) {
        on = !on;
        gpio_put(ONBOARD_LED_PIN, on);
        vTaskDelayUntil(&last, pdMS_TO_TICKS(500));
    }
}

// ----------------------------------------------------------------------------
// main
// ----------------------------------------------------------------------------
int main(void)
{
    stdio_init_all();
    debug_uart_init();

    printf("\n\n=== Pico 2 Bowling Controller - Fase 2 (USB) ===\n");
    printf("FreeRTOS SMP: configNUMBER_OF_CORES=%d\n", configNUMBER_OF_CORES);

    TaskHandle_t hb = nullptr;
    xTaskCreate(heartbeat_task, "hb", configMINIMAL_STACK_SIZE,
                nullptr, PRIO_FEEDBACK, &hb);

    if (usb_task_start(PRIO_USB) != pdPASS) {
        printf("FATAL: usb_task_start falhou\n");
        while (true) tight_loop_contents();
    }

    if (imu_task_start(PRIO_IMU) != pdPASS) {
        printf("FATAL: imu_task_start falhou\n");
        while (true) tight_loop_contents();
    }

    if (button_task_start(PRIO_BUTTON) != pdPASS) {
        printf("FATAL: button_task_start falhou\n");
        while (true) tight_loop_contents();
    }

    if (motion_tx_task_start(PRIO_MOTION_TX) != pdPASS) {
        printf("FATAL: motion_tx_task_start falhou\n");
        while (true) tight_loop_contents();
    }

    if (feedback_task_start(PRIO_FEEDBACK) != pdPASS) {
        printf("FATAL: feedback_task_start falhou\n");
        while (true) tight_loop_contents();
    }

    // Edge Impulse no Core 1 (isolado do motion). Roda gesto idle/shake.
    if (ei_inference_task_start(PRIO_EI_INFER) != pdPASS) {
        printf("FATAL: ei_inference_task_start falhou\n");
        while (true) tight_loop_contents();
    }

    // Encoder rotativo do TSW (GPIO IRQ -> queue -> task -> HID pulse).
    // NOTA: encoder_init() eh chamado DENTRO de encoder_task_fn (apos
    // scheduler iniciar) - chamar aqui em main causa IRQ storm que trava
    // outras tasks se o pino GP2 flutuar.
    if (encoder_task_start(PRIO_BUTTON) != pdPASS) {
        printf("FATAL: encoder_task_start falhou\n");
        while (true) tight_loop_contents();
    }

    vTaskStartScheduler();

    while (true) {
        tight_loop_contents();
    }
}
