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

// Параметры для тряски
#define SHAKE_THRESHOLD_G      0.4f
#define SHAKE_DETECTION_COUNT  3
#define SHAKE_COOLDOWN_MS      300
#define SHAKE_TIMEOUT_MS       1500

// Параметры кнопки
#define LONG_PRESS_MS          1800
#define SHORT_PRESS_MS         1500

// Параметры обновления
#define WEATHER_UPDATE_INTERVAL 600000UL
#define START_LOGO_DURATION_MS  2500

// Порог наклона для выбора скина на экране Select UI (~23°)
#define TILT_THRESHOLD         0.4f

// ── Скины экрана часов ───────────────────────────────────────
// Добавить новый скин: 1) добавить значение в enum
//                      2) обновить SKIN_COUNT
//                      3) реализовать в display.cpp
enum ClockSkin {
    SKIN_ARCS    = 0,   // Дуги прогресса (классика)
    SKIN_PLANETS = 1,   // Планеты на орбитах
};
#define SKIN_COUNT  2

// Magic 8-Ball ответы
extern const char* answers[];
extern const int answersCount;

#endif
