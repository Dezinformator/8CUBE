#pragma once

bool mpu_init();
void mpu_read_accel(float &gx, float &gy, float &gz);
bool mpu_is_shaking();