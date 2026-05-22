#ifndef PINS_H
#define PINS_H

// ============================================================================
// Pinout do Controle Pico 2 - Wii Bowling
// ============================================================================
// I2C0 nativo nos pinos default (GP4/GP5) para MPU6050.
// Botoes, LEDs RGB, vibra e buzzer remapeados para evitar conflito com I2C.
// UART0 (GP0/GP1) reservado para debug via conversor USB-Serial externo.
// ============================================================================

// --- MPU6050 (I2C0) ---
#define MPU6050_I2C_PORT    i2c0
#define MPU6050_SDA_PIN     4
#define MPU6050_SCL_PIN     5
#define MPU6050_I2C_FREQ    (400 * 1000)  // 400 kHz
#define MPU6050_ADDRESS     0x68

// --- Botoes (input pull-up, ativo em LOW) ---
#define BTN_PIN_A           10  // Botao A do Wiimote (confirmar)
#define BTN_PIN_B           11  // Botao B do Wiimote (segurar bola)
#define BTN_PIN_LEFT        12  // D-Pad Esquerda
#define BTN_PIN_RIGHT       13  // D-Pad Direita

// --- LEDs RGB (PWM, anodo comum ou catodo comum conforme HW) ---
// LED_R foi movido de GP13 para GP18 porque GP13 virou botao D-Pad RIGHT.
#define LED_PIN_R           18
#define LED_PIN_G           14
#define LED_PIN_B           15

// --- Motor de vibracao (PWM via MOSFET, ex: 2N7000) ---
#define VIBRA_PIN           16

// --- Buzzer passivo (PWM) ---
#define BUZZER_PIN          17

// --- UART debug (saida para conversor USB-Serial FTDI/CH340) ---
#define DEBUG_UART_TX_PIN   0
#define DEBUG_UART_RX_PIN   1
#define DEBUG_UART_BAUD     115200

// --- LED onboard do Pico 2 (status interno, GP25) ---
#define ONBOARD_LED_PIN     25

#endif // PINS_H
