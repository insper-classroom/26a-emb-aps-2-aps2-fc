// ============================================================================
// mpu6050.cpp - Driver minimalista do MPU6050 via I2C0 com diagnostico
// ============================================================================

#include "mpu6050.h"
#include "../pins.h"

#include <stdio.h>
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

// Registradores
#define REG_PWR_MGMT_1   0x6B
#define REG_WHO_AM_I     0x75
#define REG_ACCEL_XOUT_H 0x3B
#define REG_SMPLRT_DIV   0x19
#define REG_CONFIG       0x1A
#define REG_GYRO_CONFIG  0x1B
#define REG_ACCEL_CONFIG 0x1C

// WHO_AM_I retorna 0x68 no MPU6050 original; clones MPU9250/MPU6500 podem
// retornar 0x70/0x71/0x72. Aceitamos qualquer um nao-zero como "chip presente".

static int i2c_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = { reg, value };
    return i2c_write_blocking(MPU6050_I2C_PORT, MPU6050_ADDRESS, buf, 2, false);
}

static int i2c_read_reg(uint8_t reg, uint8_t *value)
{
    int w = i2c_write_blocking(MPU6050_I2C_PORT, MPU6050_ADDRESS, &reg, 1, true);
    if (w < 0) return w;
    return i2c_read_blocking(MPU6050_I2C_PORT, MPU6050_ADDRESS, value, 1, false);
}

extern "C" void mpu6050_scan_log(void)
{
    printf("[mpu] I2C scan no port=i2c0, SDA=GP%d, SCL=GP%d, freq=%d Hz\n",
           MPU6050_SDA_PIN, MPU6050_SCL_PIN, MPU6050_I2C_FREQ);
    const uint8_t addrs[] = { 0x68, 0x69 };
    for (size_t i = 0; i < sizeof(addrs) / sizeof(addrs[0]); i++) {
        uint8_t dummy;
        int r = i2c_read_blocking(MPU6050_I2C_PORT, addrs[i], &dummy, 1, false);
        printf("[mpu]   addr 0x%02X -> %s (rc=%d)\n",
               addrs[i], (r >= 0) ? "ACK" : "NACK/timeout", r);
    }
}

extern "C" bool mpu6050_init(void)
{
    // Setup do barramento.
    i2c_init(MPU6050_I2C_PORT, MPU6050_I2C_FREQ);
    gpio_set_function(MPU6050_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(MPU6050_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(MPU6050_SDA_PIN);
    gpio_pull_up(MPU6050_SCL_PIN);

    // Tempo pra estabilizar tensao no chip (datasheet: 100ms apos power-on).
    sleep_ms(100);

    mpu6050_scan_log();

    // Le WHO_AM_I antes de tudo, pra confirmar comunicacao.
    uint8_t who = 0xFF;
    int rc_who = i2c_read_reg(REG_WHO_AM_I, &who);
    printf("[mpu] WHO_AM_I(0x75) -> rc=%d val=0x%02X\n", rc_who, who);

    if (rc_who < 0) {
        printf("[mpu] ERRO: sem ACK em 0x68. Verificar fiacao SDA/SCL/VCC/GND e\n"
               "             se o modulo aceita 3V3 (alguns GY-521 precisam 5V).\n");
        return false;
    }

    if (who == 0x00 || who == 0xFF) {
        printf("[mpu] ERRO: WHO_AM_I retornou %02X (lixo). Bus instavel.\n", who);
        return false;
    }

    // Reset suave + sai do sleep.
    // PWR_MGMT_1: bit7=DEVICE_RESET, bit6=SLEEP, bits[2:0]=CLKSEL.
    int rc;
    rc = i2c_write_reg(REG_PWR_MGMT_1, 0x80);  // device reset
    printf("[mpu] PWR_MGMT_1=0x80 (reset) rc=%d\n", rc);
    sleep_ms(100);

    // CLKSEL=1 (PLL com gyro X como referencia, mais estavel que o oscilador
    // interno default). SLEEP=0.
    rc = i2c_write_reg(REG_PWR_MGMT_1, 0x01);
    printf("[mpu] PWR_MGMT_1=0x01 (wake, PLL gyroX) rc=%d\n", rc);
    sleep_ms(10);

    // Defaults explicitos (poderiamos pular - o reset ja os coloca nesses
    // valores - mas e melhor garantir):
    //   SMPLRT_DIV=0  -> sample rate = 1kHz/(1+0) = 1 kHz (gyro)
    //   CONFIG=0      -> DLPF off, gyro 8kHz, accel 1kHz
    //   GYRO_CONFIG=0 -> +/- 250 dps
    //   ACCEL_CONFIG=0-> +/- 2 g
    i2c_write_reg(REG_SMPLRT_DIV,   0x00);
    i2c_write_reg(REG_CONFIG,       0x00);
    i2c_write_reg(REG_GYRO_CONFIG,  0x00);
    i2c_write_reg(REG_ACCEL_CONFIG, 0x00);

    // Confirma que saiu mesmo do sleep relendo PWR_MGMT_1.
    uint8_t pwr = 0xFF;
    i2c_read_reg(REG_PWR_MGMT_1, &pwr);
    printf("[mpu] PWR_MGMT_1 readback = 0x%02X (esperado 0x01)\n", pwr);

    printf("[mpu] init OK\n");
    return true;
}

extern "C" void mpu6050_read_raw(int16_t accel[3], int16_t gyro[3], int16_t *temp_raw)
{
    uint8_t buffer[14];
    uint8_t reg = REG_ACCEL_XOUT_H;

    int w = i2c_write_blocking(MPU6050_I2C_PORT, MPU6050_ADDRESS, &reg, 1, true);
    if (w < 0) {
        for (int i = 0; i < 3; i++) { accel[i] = 0; gyro[i] = 0; }
        *temp_raw = 0;
        return;
    }
    i2c_read_blocking(MPU6050_I2C_PORT, MPU6050_ADDRESS, buffer, 14, false);

    for (int i = 0; i < 3; i++) {
        accel[i] = (int16_t)((buffer[i * 2] << 8) | buffer[(i * 2) + 1]);
    }
    *temp_raw = (int16_t)((buffer[6] << 8) | buffer[7]);
    for (int i = 0; i < 3; i++) {
        gyro[i] = (int16_t)((buffer[8 + i * 2] << 8) | buffer[8 + (i * 2) + 1]);
    }
}
