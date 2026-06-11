// ============================================================================
// audio.cpp - Reproducao PCM via PWM + IRQ (portado do projeto-irmao)
// ============================================================================
// Origem: C:/Embarcados/26a-emb-aps-1-dfoibandb/main/audio.c
//
// Adaptacoes para o projeto atual:
//   - Pino:        GP19 (era GP2)
//   - Semaforo:    FreeRTOS (xSemaphoreCreateBinary) em vez de pico/sync
//   - play_audio:  nao-bloqueante (timeout 0); se ja toca, descarta trigger
//   - clkdiv:      auto-ajustado pra atingir 22 MHz independente do sys_clk
//                  (o original assumia sys_clk=176 MHz com div=8; Pico 2 default
//                   eh 150 MHz, entao div ~6.82 chega no mesmo 22 MHz)
//
// Mecanica do PWM (igual ao original):
//   - PWM clock: 22 MHz, wrap=250 -> IRQ a 88 kHz
//   - Interpolacao 8x: cada amostra do array eh repetida 8 vezes pra subir
//                      de 11 kHz pra 88 kHz (wav_position >> 3)
//   - Atenuacao: 128 + (sample - 128)/VOLUME_DIV  (centra em mid-rail)
// ============================================================================

#include "audio.h"
#include "../home_sound.h"   // unico include do header com WAV_DATA[]

#include "FreeRTOS.h"
#include "semphr.h"

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"

#include <stdio.h>

#define AUDIO_PIN       19
#define VOLUME_DIV      2
#define PWM_WRAP        250
#define TARGET_PWM_HZ   22000000.0f   // 22 MHz -> 88 kHz IRQ (com wrap=250)

static volatile const uint8_t *current_wav    = NULL;
static volatile uint32_t       current_length = 0;
static volatile int32_t        wav_position   = -1;
static SemaphoreHandle_t       s_audio_sem    = NULL;

// Diagnostico: contador de IRQs (volatile, atomico em writes simples).
static volatile uint32_t       s_irq_count    = 0;

// ----------------------------------------------------------------------------
// ISR - chamada pelo PWM_IRQ_WRAP a ~88 kHz quando o slice do AUDIO_PIN faz
// wrap. ISR-safe: nada de printf, alocacao, sem.bloqueante etc.
// ----------------------------------------------------------------------------
extern "C" void pwm_audio_irq(void)
{
    pwm_clear_irq(pwm_gpio_to_slice_num(AUDIO_PIN));
    s_irq_count++;

    if (wav_position < 0 || current_wav == NULL) return;

    if (wav_position < (int32_t)(current_length << 3) - 1) {
        int sample = (int)current_wav[wav_position >> 3];
        int scaled = 128 + (sample - 128) / VOLUME_DIV;
        pwm_set_gpio_level(AUDIO_PIN, (uint16_t)scaled);
        wav_position++;
    } else {
        // Fim da reproducao - silencia saida e libera semaforo.
        pwm_set_gpio_level(AUDIO_PIN, 0);
        wav_position = -1;
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xSemaphoreGiveFromISR(s_audio_sem, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

// ----------------------------------------------------------------------------
// API publica
// ----------------------------------------------------------------------------
extern "C" void audio_init(void)
{
    if (s_audio_sem != NULL) return;  // ja inicializado, idempotente

    // Semaforo binario: comeca "livre" (1 token disponivel).
    s_audio_sem = xSemaphoreCreateBinary();
    if (s_audio_sem == NULL) return;
    xSemaphoreGive(s_audio_sem);

    gpio_set_function(AUDIO_PIN, GPIO_FUNC_PWM);
    int slice = pwm_gpio_to_slice_num(AUDIO_PIN);
    int chan  = pwm_gpio_to_channel(AUDIO_PIN);

    pwm_clear_irq(slice);
    pwm_set_irq_enabled(slice, true);
    irq_set_exclusive_handler(PWM_IRQ_WRAP, pwm_audio_irq);
    irq_set_enabled(PWM_IRQ_WRAP, true);

    pwm_config config = pwm_get_default_config();
    // Auto-ajuste do clkdiv pra atingir 22 MHz independente do sys_clk.
    float div = (float)clock_get_hz(clk_sys) / TARGET_PWM_HZ;
    pwm_config_set_clkdiv(&config, div);
    pwm_config_set_wrap(&config, PWM_WRAP);
    pwm_init(slice, &config, true);
    pwm_set_gpio_level(AUDIO_PIN, 0);

    printf("[audio] init OK pin=%d slice=%d chan=%d div=%.3f wrap=%d "
           "sys_hz=%lu irq=PWM_IRQ_WRAP\n",
           AUDIO_PIN, slice, chan, (double)div, PWM_WRAP,
           (unsigned long)clock_get_hz(clk_sys));
}

extern "C" uint32_t audio_irq_count(void)
{
    return s_irq_count;
}

extern "C" void play_home_sound(void)
{
    play_audio(WAV_DATA, WAV_DATA_LENGTH);
}

extern "C" void play_audio(const uint8_t *wav, uint32_t length)
{
    if (s_audio_sem == NULL || wav == NULL || length == 0) {
        printf("[audio] play_audio rejeitado: sem=%p wav=%p len=%lu\n",
               (void*)s_audio_sem, (const void*)wav, (unsigned long)length);
        return;
    }

    // Tenta tomar o semaforo sem bloquear. Se ja tem reproducao em andamento,
    // descarta o novo trigger - politica fire-and-forget.
    if (xSemaphoreTake(s_audio_sem, 0) != pdTRUE) {
        printf("[audio] play_audio descartado (ja tocando)\n");
        return;
    }

    current_wav    = wav;
    current_length = length;
    __dmb();              // garante visibilidade cross-core antes de armar pos
    wav_position   = 0;
    printf("[audio] play_audio: %lu samples, irq_count=%lu\n",
           (unsigned long)length, (unsigned long)s_irq_count);
}

extern "C" bool audio_is_playing(void)
{
    return wav_position >= 0;
}
