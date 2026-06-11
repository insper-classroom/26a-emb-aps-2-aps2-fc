// ============================================================================
// button_task.cpp - Le GPIO dos botoes com debounce assimetrico
// ============================================================================
// Affinity: Core 0
// Sampling: 100 Hz
// Debounce ASSIMETRICO:
//   - Press:   3 amostras consecutivas (30 ms)  -> resposta agil
//   - Release: 15 amostras consecutivas (150 ms) -> ignora bounces causados
//                                                   pela vibracao do swing
// Acoplamento: chama diretamente usb_hid_set_buttons() (rapido, mutex-safe).
// ============================================================================

#include "button_task.h"
#include "usb_task.h"
#include "../pins.h"

#include "FreeRTOS.h"
#include "task.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/watchdog.h"

#include <stdio.h>

// Combo de RESET: segurar A + B simultaneamente por 3 segundos.
// Quando CDC USB engasga e Dolphin para de ler motion, pressiona A+B juntos
// e segura - o firmware reboota via watchdog (USB re-enumera limpo).
#define COMBO_RESET_MS  3000

#define BTN_TASK_STACK          (configMINIMAL_STACK_SIZE * 2)
#define BTN_TASK_CORE_AFFINITY  (1u << 0)  // Core 0
#define BTN_SAMPLE_PERIOD_MS    10

// Debounce assimetrico: rapido pra press, lento pra release.
// 30 ms basta pra evitar chatter eletrico no fechamento do contato;
// 150 ms ignora trepidacao mecanica durante swing intenso.
#define BTN_PRESS_SAMPLES       3
#define BTN_RELEASE_SAMPLES     15

struct btn_entry_t {
    uint8_t pin;
    uint8_t bit;
    uint8_t streak;     // amostras consecutivas com o estado novo (oposto ao atual)
    bool current;       // estado confirmado (true = pressionado)
};

static btn_entry_t s_buttons[] = {
    { BTN_PIN_A,     BTN_BIT_A,     0, false },  // TSW S1 (central)
    { BTN_PIN_B,     BTN_BIT_B,     0, false },  // botao EXTERNO
    { BTN_PIN_LEFT,  BTN_BIT_LEFT,  0, false },  // TSW S?
    { BTN_PIN_RIGHT, BTN_BIT_RIGHT, 0, false },  // TSW S?
    { BTN_PIN_UP,    BTN_BIT_UP,    0, false },  // TSW S?
    { BTN_PIN_DOWN,  BTN_BIT_DOWN,  0, false },  // TSW S?
};
static const size_t N_BTNS = sizeof(s_buttons) / sizeof(s_buttons[0]);

static void btn_gpio_init_all(void)
{
    for (size_t i = 0; i < N_BTNS; i++) {
        gpio_init(s_buttons[i].pin);
        gpio_set_dir(s_buttons[i].pin, GPIO_IN);
        gpio_pull_up(s_buttons[i].pin);  // ativo em LOW
    }
}

static void button_task_fn(void *p)
{
    (void)p;
    btn_gpio_init_all();

    uint32_t mask = 0;
    TickType_t last = xTaskGetTickCount();
    TickType_t combo_start = 0;  // tick em que A+B comecaram a ser segurados

    while (true) {
        bool changed = false;
        for (size_t i = 0; i < N_BTNS; i++) {
            // GPIO em pull-up -> botao pressionado puxa para LOW.
            bool sample = (gpio_get(s_buttons[i].pin) == 0);

            if (sample != s_buttons[i].current) {
                // Estado divergente do confirmado: acumula streak na nova direcao.
                if (s_buttons[i].streak < 0xFF) s_buttons[i].streak++;

                // Threshold depende da direcao da mudanca.
                uint8_t needed = sample ? BTN_PRESS_SAMPLES
                                        : BTN_RELEASE_SAMPLES;

                if (s_buttons[i].streak >= needed) {
                    s_buttons[i].current = sample;
                    s_buttons[i].streak  = 0;
                    changed = true;
                    printf("[btn] %s -> %d\n",
                           (s_buttons[i].bit == BTN_BIT_A)     ? "A" :
                           (s_buttons[i].bit == BTN_BIT_B)     ? "B" :
                           (s_buttons[i].bit == BTN_BIT_LEFT)  ? "LEFT" :
                           (s_buttons[i].bit == BTN_BIT_RIGHT) ? "RIGHT" :
                           (s_buttons[i].bit == BTN_BIT_UP)    ? "UP" :
                           (s_buttons[i].bit == BTN_BIT_DOWN)  ? "DOWN" : "?",
                           sample ? 1 : 0);
                }
            } else {
                // Voltou ao estado confirmado: zera o streak (era ruido).
                s_buttons[i].streak = 0;
            }

            if (s_buttons[i].current) mask |=  (1u << s_buttons[i].bit);
            else                      mask &= ~(1u << s_buttons[i].bit);
        }

        if (changed) {
            usb_hid_set_buttons(mask);
        }

        // Detector de combo RESET (A + B segurados juntos por 3s).
        bool a_pressed = (mask & (1u << BTN_BIT_A)) != 0;
        bool b_pressed = (mask & (1u << BTN_BIT_B)) != 0;
        if (a_pressed && b_pressed) {
            if (combo_start == 0) {
                combo_start = xTaskGetTickCount();
                printf("[btn] combo A+B iniciado - segura 3s pra reset\n");
            } else if ((xTaskGetTickCount() - combo_start) >= pdMS_TO_TICKS(COMBO_RESET_MS)) {
                printf("[btn] >>> COMBO RESET! watchdog reboot em 100ms\n");
                watchdog_reboot(0, 0, 100);
                while (true) { tight_loop_contents(); }
            }
        } else {
            combo_start = 0;
        }

        vTaskDelayUntil(&last, pdMS_TO_TICKS(BTN_SAMPLE_PERIOD_MS));
    }
}

extern "C" BaseType_t button_task_start(UBaseType_t priority)
{
    TaskHandle_t handle = nullptr;
    BaseType_t rc = xTaskCreate(button_task_fn, "btn", BTN_TASK_STACK,
                                nullptr, priority, &handle);
    if (rc != pdPASS) return rc;
    vTaskCoreAffinitySet(handle, BTN_TASK_CORE_AFFINITY);
    return pdPASS;
}
