// ============================================================================
// encoder.cpp - Decodificador de quadratura do encoder rotativo do TSWB-3N
// ============================================================================
// Hardware:
//   - GP2 = phase A (input, pull-up)
//   - GP3 = phase B (input, pull-up)
//   - COM A do TSW = GND
//
// Decodificacao:
//   - GPIO IRQ em ambas as bordas (RISE + FALL) do A
//   - Na ISR, le B e determina sentido: A == B  -> CCW (-1)
//                                       A != B  -> CW  (+1)
//   - Debounce 2ms em software pra evitar IRQ storm causado por ruido no
//     fio ou bouncing mecanico dos contatos
//   - Posta o direction (int8_t) em g_encoder_queue via xQueueSendFromISR
//
// NOTA: encoder_init() DEVE ser chamada de dentro de uma task (apos
// vTaskStartScheduler). Chamar antes do scheduler causa IRQ storm que trava
// outras tasks (botoes ficam sem responder).
// ============================================================================

#include "encoder.h"
#include "../pins.h"

#include "FreeRTOS.h"
#include "queue.h"

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "pico/time.h"

#define ENCODER_QUEUE_LEN   32
#define ENC_DEBOUNCE_US     2000   // ignora IRQs com menos de 2ms entre si

QueueHandle_t g_encoder_queue = nullptr;

// Timestamp da ultima IRQ aceita (pra debounce em sw).
static volatile uint64_t s_last_irq_us = 0;

// ISR: chamada em RISE ou FALL de ENC_PIN_A.
extern "C" void encoder_gpio_irq(uint gpio, uint32_t events)
{
    (void)events;
    if (gpio != ENC_PIN_A) return;

    // Debounce: descarta IRQs muito proximas (ruido no fio ou bouncing).
    uint64_t now = time_us_64();
    if (now - s_last_irq_us < ENC_DEBOUNCE_US) return;
    s_last_irq_us = now;

    bool a = gpio_get(ENC_PIN_A);
    bool b = gpio_get(ENC_PIN_B);
    int8_t dir = (a == b) ? (int8_t)-1 : (int8_t)+1;

    BaseType_t higher_woken = pdFALSE;
    xQueueSendFromISR(g_encoder_queue, &dir, &higher_woken);
    portYIELD_FROM_ISR(higher_woken);
}

extern "C" void encoder_init(void)
{
    if (g_encoder_queue != nullptr) return;  // idempotente

    g_encoder_queue = xQueueCreate(ENCODER_QUEUE_LEN, sizeof(int8_t));
    if (g_encoder_queue == nullptr) return;

    gpio_init(ENC_PIN_A);
    gpio_set_dir(ENC_PIN_A, GPIO_IN);
    gpio_pull_up(ENC_PIN_A);

    gpio_init(ENC_PIN_B);
    gpio_set_dir(ENC_PIN_B, GPIO_IN);
    gpio_pull_up(ENC_PIN_B);

    // Inicializa o timestamp antes de habilitar a IRQ.
    s_last_irq_us = time_us_64();

    gpio_set_irq_enabled_with_callback(
        ENC_PIN_A,
        GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL,
        true,
        &encoder_gpio_irq
    );
}
