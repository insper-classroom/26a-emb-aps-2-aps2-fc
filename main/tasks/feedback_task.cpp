// ============================================================================
// feedback_task.cpp - Sequencia de boot com LED + buzzer + vibra
// ============================================================================
// LED de status: GP14 (originalmente LED_PIN_G no pins.h - usamos como LED
// unico ja que o controle tem apenas 1 LED por enquanto).
// Buzzer:        GP17 via PWM (tom audivel via buzzer passivo).
// Vibra:         GP16 via GPIO direto (motor via MOSFET).
// ============================================================================

#include "feedback_task.h"
#include "../pins.h"
#include "../audio/audio.h"

#include "FreeRTOS.h"
#include "task.h"

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "tusb.h"

#include <stdio.h>

#define FEEDBACK_STACK   (configMINIMAL_STACK_SIZE * 2)

// LED de status - usa o pino que era LED_PIN_G (GP14).
// Se voce conectou o LED em outro pino (GP15 LED_B ou GP18 LED_R), basta
// trocar aqui pra LED_PIN_B ou LED_PIN_R.
#define STATUS_LED_PIN   LED_PIN_G

// ----------------------------------------------------------------------------
// LED helpers (GPIO digital simples)
// ----------------------------------------------------------------------------
static void led_init(void)
{
    gpio_init(STATUS_LED_PIN);
    gpio_set_dir(STATUS_LED_PIN, GPIO_OUT);
    gpio_put(STATUS_LED_PIN, 0);
}

static inline void led_on(void)  { gpio_put(STATUS_LED_PIN, 1); }
static inline void led_off(void) { gpio_put(STATUS_LED_PIN, 0); }

// ----------------------------------------------------------------------------
// Vibra (GPIO simples ligado a MOSFET ou driver de motor)
//
// Modulos comerciais usam logicas diferentes:
//   - Active HIGH (default): SIG=1 -> motor ON, SIG=0 -> motor OFF
//   - Active LOW (alguns):   SIG=0 -> motor ON, SIG=1 -> motor OFF
//
// Se o seu modulo deixa o motor ligado o tempo todo com GPIO em LOW,
// defina VIBRA_ACTIVE_LOW=1 ao compilar (ou descomente abaixo).
// ----------------------------------------------------------------------------
// #define VIBRA_ACTIVE_LOW 1

#ifndef VIBRA_ACTIVE_LOW
#define VIBRA_ACTIVE_LOW 0
#endif

#if VIBRA_ACTIVE_LOW
  #define VIBRA_ON_LEVEL  0
  #define VIBRA_OFF_LEVEL 1
#else
  #define VIBRA_ON_LEVEL  1
  #define VIBRA_OFF_LEVEL 0
#endif

static void vibra_init(void)
{
    gpio_init(VIBRA_PIN);
    gpio_set_dir(VIBRA_PIN, GPIO_OUT);
    gpio_put(VIBRA_PIN, VIBRA_OFF_LEVEL);
}

static void vibra_pulse(uint32_t duration_ms)
{
    gpio_put(VIBRA_PIN, VIBRA_ON_LEVEL);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    gpio_put(VIBRA_PIN, VIBRA_OFF_LEVEL);
}

// ----------------------------------------------------------------------------
// Buzzer (PWM gerando um square wave audivel)
// ----------------------------------------------------------------------------
static void buzzer_init(void)
{
    // Inicia como GPIO LOW (silencio) - so vira PWM quando toca.
    gpio_init(BUZZER_PIN);
    gpio_set_dir(BUZZER_PIN, GPIO_OUT);
    gpio_put(BUZZER_PIN, 0);
}

static void buzzer_tone(uint32_t freq_hz, uint32_t duration_ms)
{
    if (freq_hz < 30 || freq_hz > 20000) return;

    gpio_set_function(BUZZER_PIN, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(BUZZER_PIN);
    uint chan  = pwm_gpio_to_channel(BUZZER_PIN);

    // Base = 125 MHz / clkdiv = 500 kHz com clkdiv=250.
    pwm_set_clkdiv(slice, 250.0f);
    uint32_t wrap = 500000U / freq_hz;
    if (wrap == 0)     wrap = 1;
    if (wrap > 65535)  wrap = 65535;
    pwm_set_wrap(slice, (uint16_t)(wrap - 1));
    pwm_set_chan_level(slice, chan, (uint16_t)(wrap / 2));  // 50% duty
    pwm_set_enabled(slice, true);

    vTaskDelay(pdMS_TO_TICKS(duration_ms));

    pwm_set_enabled(slice, false);
    // Volta GPIO LOW pra cortar o som sem deixar PWM travado.
    gpio_set_function(BUZZER_PIN, GPIO_FUNC_SIO);
    gpio_put(BUZZER_PIN, 0);
}

// ----------------------------------------------------------------------------
// Task: sequencia de boot
// ----------------------------------------------------------------------------
static void feedback_task_fn(void *p)
{
    (void)p;
    led_init();
    vibra_init();
    buzzer_init();
    // NOTA: audio_init() foi MOVIDO pra ei_inference_task (Core 1) - assim a
    // PWM_IRQ_WRAP de 88kHz fica isolada do Core 0 (onde a USB roda) e nao
    // causa travamentos esporadicos do CDC.

    printf("[fb] power-on indicator\n");

    // Etapa 1: "tem energia, controle vivo"
    buzzer_tone(440, 100);          // bip grave 100 ms
    vibra_pulse(150);               // vibra 150 ms

    // Etapa 2: espera USB enumerar (max ~5s)
    printf("[fb] aguardando USB...\n");
    int wait_ticks = 0;
    while (!tud_mounted()) {
        // Pisca LED devagar enquanto aguarda
        led_on();
        vTaskDelay(pdMS_TO_TICKS(250));
        led_off();
        vTaskDelay(pdMS_TO_TICKS(250));
        if (++wait_ticks > 20) break;  // ~10s timeout, segue mesmo assim
    }

    // Etapa 3: "PC reconheceu, pronto pra jogar"
    printf("[fb] USB %s - LED steady\n", tud_mounted() ? "OK" : "TIMEOUT");
    led_on();
    buzzer_tone(880, 80);           // bip agudo 80 ms

    // Boot completo. LED fica aceso, task suspende (libera RAM stack pro RTOS).
    vTaskSuspend(NULL);
}

extern "C" BaseType_t feedback_task_start(UBaseType_t priority)
{
    TaskHandle_t handle = nullptr;
    BaseType_t rc = xTaskCreate(feedback_task_fn, "feedback",
                                FEEDBACK_STACK, nullptr, priority, &handle);
    if (rc != pdPASS) return rc;
    // Sem affinity fixa - pode rodar em qualquer core
    return pdPASS;
}
