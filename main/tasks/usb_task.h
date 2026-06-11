#ifndef USB_TASK_H
#define USB_TASK_H

#include <stdint.h>
#include "FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

// Cria a usb_task (Core 0, prio alta). Inicializa TinyUSB e roda tud_task()
// em loop. Deve ser chamada antes de vTaskStartScheduler(). Retorna pdPASS
// em sucesso.
BaseType_t usb_task_start(UBaseType_t priority);

// Atualiza o estado de botoes do HID Gamepad. Envia o relatorio apenas
// quando o estado muda - thread-safe via mutex interno.
//   buttons_mask: bitmask de 32 botoes (bit 0 = botao 0, etc.)
//
// Mapeamento sugerido:
//   bit 0 = Botao A
//   bit 1 = Botao B
//   bit 2 = Botao "-" / Minus
//   bit 3 = Botao "+" / Plus (futuro)
//   bit 4 = Botao HOME (acionado pela EI)
//   bit 5 = Botao 1
//   bit 6 = Botao 2
void usb_hid_set_buttons(uint32_t buttons_mask);

// Envia uma pulse de press+release num botao especifico (usado pela EI
// pra disparar HOME por exemplo). press_ms = duracao do "press".
void usb_hid_pulse_button(uint8_t bit_index, uint32_t press_ms);

// Escreve no CDC. Nao bloqueia (drop se buffer cheio). Use pra motion.
void usb_cdc_write(const void *data, uint32_t len);

// Diagnostico: contadores acumulados do CDC TX (writes OK vs descartados).
void usb_cdc_stats(uint32_t *ok, uint32_t *drop, uint32_t *last_avail);

#ifdef __cplusplus
}
#endif

#endif // USB_TASK_H
