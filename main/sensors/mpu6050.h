#ifndef MPU6050_H
#define MPU6050_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Inicializa I2C0 (GP4 SDA / GP5 SCL @ 400 kHz), tira o MPU6050 do sleep
// e valida WHO_AM_I (0x75). Loga o diagnostico via printf (UART).
// Retorna true em sucesso. Em falha, segue tentando ler mesmo assim para
// nao travar o sistema, mas as leituras virao todas zero.
bool mpu6050_init(void);

// Le 14 bytes consecutivos a partir de ACCEL_XOUT_H (0x3B):
//   - accel[0..2]: aceleracao raw X, Y, Z
//   - temp_raw:    temperatura raw
//   - gyro[0..2]:  giroscopio raw X, Y, Z
void mpu6050_read_raw(int16_t accel[3], int16_t gyro[3], int16_t *temp_raw);

// Faz um scan dos enderecos 0x68 e 0x69 e loga quem respondeu (debug).
void mpu6050_scan_log(void);

#ifdef __cplusplus
}
#endif

#endif // MPU6050_H
