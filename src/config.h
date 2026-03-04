#pragma once
#ifndef CONFIG_H
#define CONFIG_H

// Пины ESP32-C3 Super Mini
#define TFT_CS    7
#define TFT_DC    3
#define TFT_RST   2
#define TOUCH_PIN 10
#define SDA_PIN   21
#define SCL_PIN   20

// Wi-Fi
extern const char* WIFI_SSID;
extern const char* WIFI_PASSWORD;

// ── Яндекс Погода ────────────────────────────────────────────
extern const char* YANDEX_WEATHER_KEY;   // API ключ
extern const char* WEATHER_LAT;          // Широта
extern const char* WEATHER_LON;          // Долгота
extern const char* WEATHER_CITY;         // Название для отображения на экране

// NTP
extern const char* NTP_SERVER;
extern const long  GMT_OFFSET_SEC;
extern const int   DAYLIGHT_OFFSET_SEC;

// Тряска
#define SHAKE_THRESHOLD_G      0.4f
#define SHAKE_DETECTION_COUNT  3
#define SHAKE_COOLDOWN_MS      300
#define SHAKE_TIMEOUT_MS       1500

// Кнопка
#define LONG_PRESS_MS   1800
#define SHORT_PRESS_MS  1500

// Обновления
#define WEATHER_UPDATE_INTERVAL 600000UL   // 10 минут
#define START_LOGO_DURATION_MS  2500

// Порог наклона для выбора скина
#define TILT_THRESHOLD  0.4f

// ── Скины часов ───────────────────────────────────────────────
enum ClockSkin {
    SKIN_ARCS    = 0,
    SKIN_PLANETS = 1,
};
#define SKIN_COUNT 2

// ── Индексы иконок погоды ─────────────────────────────────────
// Используются в weather_id — не зависят от конкретного API
#define WX_SUNNY       0
#define WX_FEW_CLOUDS  1
#define WX_SCATTERED   2
#define WX_BROKEN      3
#define WX_OVERCAST    4
#define WX_RAIN        5
#define WX_SNOW        6
#define WX_THUNDER     7

// Magic 8-Ball
extern const char* answers[];
extern const int   answersCount;

#endif