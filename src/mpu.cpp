#include "mpu.h"
#include "config.h"
#include <Wire.h>
#include <math.h>
#include <Arduino.h>

#define MPU_ADDR             0x68
#define MPU_PWR_MGMT_1       0x6B
#define MPU_WHO_AM_I         0x75
#define MPU_ACCEL_XOUT_H     0x3B

static int16_t ax, ay, az;
static float base_g = 1.0;  // Базовое значение G (калибровка)

bool mpu_init() {
    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(100000);

    // Проверка соединения
    Wire.beginTransmission(MPU_ADDR);
    if (Wire.endTransmission() != 0) {
        Serial.println("MPU not found");
        return false;
    }

    // Пробуждение MPU
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(MPU_PWR_MGMT_1);
    Wire.write(0x00);
    if (Wire.endTransmission() != 0) {
        Serial.println("Failed to wake MPU");
        return false;
    }

    delay(150);

    // Проверка WHO_AM_I
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(MPU_WHO_AM_I);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(MPU_ADDR, 1) != 1) return false;

    uint8_t who = Wire.read();
    Serial.print("MPU WHO_AM_I: 0x");
    Serial.println(who, HEX);
    
    // Калибровка - усредняем несколько чтений
    Serial.println("Calibrating MPU...");
    float sum = 0;
    for(int i = 0; i < 50; i++) {
        float gx, gy, gz;
        mpu_read_accel(gx, gy, gz);
        float g = sqrt(gx*gx + gy*gy + gz*gz);
        sum += g;
        delay(10);
    }
    base_g = sum / 50;
    Serial.print("Base G: ");
    Serial.println(base_g, 2);
    
    return (who == 0x68 || who == 0x70);
}

void mpu_read_accel(float &gx, float &gy, float &gz) {
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(MPU_ACCEL_XOUT_H);
    if (Wire.endTransmission(false) != 0) {
        gx = 0.0f;
        gy = 0.0f;
        gz = 0.0f;
        return;
    }
    if (Wire.requestFrom(MPU_ADDR, 6) != 6) {
        gx = 0.0f;
        gy = 0.0f;
        gz = 0.0f;
        return;
    }
    ax = (Wire.read() << 8) | Wire.read();
    ay = (Wire.read() << 8) | Wire.read();
    az = (Wire.read() << 8) | Wire.read();
    
    gx = ax / 16384.0f;
    gy = ay / 16384.0f;
    gz = az / 16384.0f;
}

bool mpu_is_shaking() {
    float gx, gy, gz;
    mpu_read_accel(gx, gy, gz);
    float g = sqrt(gx*gx + gy*gy + gz*gz);
    
    // Используем динамический порог относительно базового значения
    float threshold = base_g + SHAKE_THRESHOLD_G;
    
    static unsigned long lastShakeTime = 0;
    static int shakeCount = 0;
    static float lastG = 1.0;
    
    // Сглаживание
    float smoothedG = lastG * 0.7 + g * 0.3;
    lastG = smoothedG;
    
    // Проверяем превышение порога
    bool shaking = (smoothedG > threshold);
    
    // Антидребезг
    if (shaking) {
        if (millis() - lastShakeTime > 100) {  // Минимум 100мс между детектами
            lastShakeTime = millis();
            return true;
        }
    }
    
    return false;
}

// Новая функция для получения сырых данных с калибровкой
void mpu_get_calibrated(float &gx, float &gy, float &gz) {
    mpu_read_accel(gx, gy, gz);
    // Здесь можно добавить калибровку если нужно
}