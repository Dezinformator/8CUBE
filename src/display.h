#pragma once

#include <Adafruit_GC9A01A.h>

extern Adafruit_GC9A01A tft;
extern bool mpu_ok;  // Добавлено для доступа к статусу MPU

void display_init();
void display_show_logo();
void display_clear();
void display_draw_clock();
void display_draw_weather();
void display_draw_magic_ball();
void display_show_prediction();
void display_draw_arc_ring(uint16_t color, float progress, int radius, int thickness);