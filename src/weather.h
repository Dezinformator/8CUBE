#pragma once
#include <Arduino.h>

extern float temperature;
extern int   humidity;
extern int   weather_id;   // Код погоды OpenWeatherMap (800, 801, 802...)
extern String description;

void connect_wifi();
void update_weather();
