#ifndef IMU_TYPES_H
#define IMU_TYPES_H

#include <stdint.h>

// Amostra unica do MPU6050 publicada pela imu_task.
// Valores raw (int16) - escala depende do range configurado.
// Por padrao MPU6050 boota em +/-2g (accel) e +/-250dps (gyro).
struct imu_sample_t {
    int16_t accel[3];       // X, Y, Z
    int16_t gyro[3];        // X, Y, Z
    int16_t temp_raw;       // temperatura raw (opcional, normalmente nao usado)
    uint32_t timestamp_ms;  // tick em ms quando a amostra foi colhida
};

#endif // IMU_TYPES_H
