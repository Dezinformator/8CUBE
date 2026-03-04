#pragma once

#include <Adafruit_GC9A01A.h>
#include "config.h"

extern Adafruit_GC9A01A tft;
extern bool mpu_ok;

void display_init();
void display_show_logo();
void display_show_ap_screen();  // Экран режима точки доступа
void display_clear();

// Экран выбора скина — показывается один раз при старте.
// Возвращает выбранный скин (SKIN_ARCS или SKIN_PLANETS).
// Управление: наклон влево/вправо — листать, короткое нажатие — выбрать.
ClockSkin display_select_skin(ClockSkin current);

// Экран часов — рисует скин, сохранённый в activeSkin
void display_draw_clock();

// Установить активный скин (вызывается из main после загрузки из Preferences)
void display_set_skin(ClockSkin skin);
ClockSkin display_get_skin();  // Получить текущий активный скин

void display_draw_weather();
void display_draw_magic_ball();
void display_show_prediction();
void display_draw_arc_ring(uint16_t color, float progress, int radius, int thickness);

// Статус синхронизации времени — отображается на экране часов
// когда время ещё не получено
void display_set_time_status(const char* status);
