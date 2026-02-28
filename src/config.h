#pragma once

#ifndef CONFIG_H
#define CONFIG_H

// Пины для ESP32-C3 Super Mini
#define TFT_CS     7
#define TFT_DC     3
#define TFT_RST    2
#define TOUCH_PIN  10
#define SDA_PIN    21
#define SCL_PIN    20

// Wi-Fi
extern const char* WIFI_SSID;
extern const char* WIFI_PASSWORD;

// Погода
extern const char* CITY;
extern const char* API_KEY;

// NTP
extern const char* NTP_SERVER;
extern const long  GMT_OFFSET_SEC;
extern const int   DAYLIGHT_OFFSET_SEC;

// Параметры для тряски - значительно снижаем порог
#define SHAKE_THRESHOLD_G      0.4f    // Было 1.1, теперь 0.4
#define SHAKE_DETECTION_COUNT  3       // 3 тряски достаточно
#define SHAKE_COOLDOWN_MS      300     // Минимальное время между трясками (мс)
#define SHAKE_TIMEOUT_MS       1500    // Максимальное время для набора 3 трясок

// Параметры кнопки
#define LONG_PRESS_MS          1800
#define SHORT_PRESS_MS         1500

// Параметры обновления
#define WEATHER_UPDATE_INTERVAL 600000UL     // 10 минут
#define START_LOGO_DURATION_MS  2500

// Magic 8-Ball ответы
extern const char* answers[];
extern const int answersCount;

#endif