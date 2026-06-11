#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Inicializa PWM em AUDIO_PIN (GP19) e instala o IRQ handler.
// Deve ser chamada DEPOIS de vTaskStartScheduler (de dentro de alguma task),
// porque usa semaforo FreeRTOS.
void audio_init(void);

// Toca um array PCM 8-bit unsigned mono @ 11 kHz. NAO bloqueia.
// Se uma reproducao ja estiver em andamento, descarta o trigger novo
// (fire-and-forget, perfeito pra disparo via gesto/eventos).
void play_audio(const uint8_t *wav, uint32_t length);

// True se uma reproducao esta em andamento agora.
bool audio_is_playing(void);

// Diagnostico: contador de IRQs do PWM_IRQ_WRAP desde init.
// Se voltar 0 apos varios segundos, a IRQ nao esta sendo disparada.
uint32_t audio_irq_count(void);

// Wrapper que toca o som do Wii Home (WAV_DATA do home_sound.h).
// O array eh referenciado dentro do modulo audio pra evitar duplo include
// (home_sound.h define o array sem extern -> so 1 .cpp pode inclui-lo).
void play_home_sound(void);

#ifdef __cplusplus
}
#endif

#endif // AUDIO_H
