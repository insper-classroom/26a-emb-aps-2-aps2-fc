#ifndef PINS_H
#define PINS_H

// ============================================================================
// Pinout do Controle Pico 2 - Wii Bowling
// ============================================================================
// I2C0 nativo nos pinos default (GP4/GP5) para MPU6050.
// Botoes do TSWB-3N + botao B externo. Encoder rotativo via GPIO IRQ.
// UART0 (GP0/GP1) reservado para debug via conversor USB-Serial externo.
// ============================================================================

// --- MPU6050 (I2C0) ---
#define MPU6050_I2C_PORT    i2c0
#define MPU6050_SDA_PIN     4
#define MPU6050_SCL_PIN     5
#define MPU6050_I2C_FREQ    (400 * 1000)  // 400 kHz
#define MPU6050_ADDRESS     0x68

// --- Botoes (input pull-up, ativo em LOW) ---
// TSWB-3N: 5 push buttons agrupados no lado esquerdo do Pico (GP10-15,
// pulando GP14 que e o LED de status). Orientacao S2-S5 depende da
// montagem fisica - ajustar UP/DOWN/LEFT/RIGHT apos teste no joy.cpl.
// Botao B externo no lado direito (GP20) pra ficar acessivel ao polegar
// oposto durante o swing. COM A e COM B do TSW vao para GND do Pico.
#define BTN_PIN_A           10  // TSW S1 (central push) -> A
#define BTN_PIN_UP          11  // TSW S? (ajustar apos teste)
#define BTN_PIN_RIGHT       12  // TSW S? (ajustar apos teste)
#define BTN_PIN_DOWN        13  // TSW S? (ajustar apos teste)
#define BTN_PIN_LEFT        15  // TSW S? (ajustar apos teste)
#define BTN_PIN_B           20  // botao EXTERNO -> B (hold da bola), lado direito

// --- Encoder rotativo do TSW (quadratura, 24 detents, 12 pulses/360°) ---
// Cada detent = pulso de LEFT ou RIGHT no HID (decodificado por ISR + queue).
#define ENC_PIN_A           2
#define ENC_PIN_B           3

// --- LEDs RGB (PWM, anodo comum ou catodo comum conforme HW) ---
// Apenas LED_G eh efetivamente usado como STATUS LED pelo feedback_task.
// LED_R e LED_B sao reservados pra futuro (se houver feedback colorido).
// LED_B foi movido de GP15 para GP21 porque GP15 virou TSW S5.
#define LED_PIN_R           18
#define LED_PIN_G           14
#define LED_PIN_B           21

// --- Motor de vibracao (PWM via MOSFET, ex: 2N7000) ---
#define VIBRA_PIN           16

// --- Buzzer passivo (PWM) - bipes de boot ---
#define BUZZER_PIN          17

// --- Speaker PCM (PWM + IRQ + filtro RC) - som do Wii Home ---
// Definido em audio/audio.cpp como AUDIO_PIN=19. Listado aqui pra referencia.
#define AUDIO_PIN_REF       19

// --- UART debug (saida para conversor USB-Serial FTDI/CH340) ---
#define DEBUG_UART_TX_PIN   0
#define DEBUG_UART_RX_PIN   1
#define DEBUG_UART_BAUD     115200

// --- LED onboard do Pico 2 (status interno, GP25) ---
#define ONBOARD_LED_PIN     25

#endif // PINS_H
