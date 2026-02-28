#include <Arduino.h>
#include "config.h"
#include "display.h"
#include "mpu.h"
#include "weather.h"
#include <time.h>
#include <WiFi.h>
#include <math.h>
#include <esp_sntp.h>

// Глобальные определения
const char* WIFI_SSID     = "SiriusAirusHaus";
const char* WIFI_PASSWORD = "Popova20";

const char* CITY          = "Smolensk";
const char* API_KEY       = "7540c5b2701a1bf3e9cba8e3baf33214";

const char* NTP_SERVER           = "time.google.com";
const long  GMT_OFFSET_SEC       = 10800;
const int   DAYLIGHT_OFFSET_SEC  = 0;

const char* answers[] = {
  "Yes",
  "No",
  "Maybe",
  "No way",
  "Ask later",
  "Not now",
  "Likely",
  "Unlikely",
  "Certainly",
  "Never"
};
const int answersCount = 10;

// Глобальные переменные
bool displayActive = true;
int currentMode = 0;
bool needRedraw = true;
unsigned long lastWeatherUpdate = 0;
static unsigned long touchStart = 0;
static int lastButtonState = LOW;
static unsigned long lastShakeCheck = 0;

bool mpu_ok = false;
bool wifi_ok = false;
unsigned long lastDiagnosticOutput = 0;

void printDiagnostic() {
    Serial.println("\n=== STATUS ===");
    Serial.print("Display: ");
    Serial.println(displayActive ? "ON" : "OFF");
    Serial.print("Mode: ");
    switch(currentMode) {
        case 0: Serial.println("CLOCK"); break;
        case 1: Serial.println("WEATHER"); break;
        case 2: Serial.println("MAGIC BALL"); break;
    }
    Serial.print("WiFi: ");
    Serial.println(wifi_ok ? "OK" : "NO");
    Serial.print("MPU: ");
    Serial.println(mpu_ok ? "OK" : "NO");
    Serial.println("=============\n");
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n\n=== EIGHT CUBE STARTING ===");

    // Настройка кнопки
    pinMode(TOUCH_PIN, INPUT);

    // Инициализация дисплея
    display_init();

    // Показываем логотип
    display_show_logo();
    delay(START_LOGO_DURATION_MS);

    // Инициализация MPU (нужен до экрана выбора скина — там читаем наклон)
    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(100000);
    mpu_ok = mpu_init();

    // ── Экран выбора скина ────────────────────────────────────
    // Показывается при каждом запуске после логотипа.
    // Наклон влево/вправо — листать, короткое нажатие — выбрать.
    ClockSkin chosen = display_select_skin(SKIN_ARCS);
    display_set_skin(chosen);
    Serial.printf("Skin selected: %d\n", (int)chosen);

    // Подключение к WiFi
    connect_wifi();

    if (WiFi.status() == WL_CONNECTED) {
        wifi_ok = true;

        // Запускаем синхронизацию времени
        configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);

        // Ждём реальной синхронизации (не просто "что-то есть в буфере")
        Serial.print("Waiting for NTP sync");
        int attempts = 0;
        while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED && attempts < 40) {
            delay(500);
            Serial.print(".");
            attempts++;
        }
        Serial.println();

        if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
            Serial.println("NTP sync COMPLETED");
        } else {
            Serial.println("NTP sync TIMEOUT — time may be incorrect");
        }

        // Диагностика: выводим полученное время
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            Serial.printf("NTP time: %02d:%02d:%02d\n",
                timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        }

        Serial.printf("Time sync after %lu ms from boot\n", millis());

        // Получаем погоду
        update_weather();
        lastWeatherUpdate = millis();
    }

    randomSeed(analogRead(0));

    Serial.println("\n=== READY ===\n");

    display_clear();
    displayActive = true;
    needRedraw = true;
}

void loop() {
    // Периодическая диагностика (раз в 30 секунд)
    if (millis() - lastDiagnosticOutput > 30000) {
        printDiagnostic();
        lastDiagnosticOutput = millis();
    }

    // Режим сна — проверяем только тряску
    if (!displayActive) {
        if (millis() - lastShakeCheck > 200) {
            if (mpu_ok && mpu_is_shaking()) {
                displayActive = true;
                display_clear();
                needRedraw = true;
                Serial.println(">>> WAKE UP <<<");
            }
            lastShakeCheck = millis();
        }
        delay(50);
        return;
    }

    // Чтение состояния кнопки
    int currentButtonState = digitalRead(TOUCH_PIN);

    if (currentButtonState == HIGH && lastButtonState == LOW) {
        touchStart = millis();
        Serial.println("Button PRESSED");
    }

    if (currentButtonState == LOW && lastButtonState == HIGH) {
        if (touchStart > 0) {
            unsigned long duration = millis() - touchStart;

            if (duration < LONG_PRESS_MS && duration > 50) {
                // Короткое нажатие — смена режима
                currentMode = (currentMode + 1) % 3;
                display_clear();
                needRedraw = true;
                Serial.print("Mode: ");
                switch(currentMode) {
                    case 0: Serial.println("CLOCK"); break;
                    case 1: Serial.println("WEATHER"); break;
                    case 2: Serial.println("MAGIC BALL"); break;
                }
            }
            touchStart = 0;
        }
    }

    // Длинное нажатие — выключение
    if (touchStart > 0 && (millis() - touchStart > LONG_PRESS_MS)) {
        displayActive = false;
        display_clear();
        touchStart = 0;
        Serial.println(">>> SLEEP <<<");
        delay(300);
        return;
    }

    lastButtonState = currentButtonState;

    // Отрисовка при необходимости
    if (needRedraw) {
        switch (currentMode) {
            case 0: display_draw_clock(); break;
            case 1: display_draw_weather(); break;
            case 2: display_draw_magic_ball(); break;
        }
        needRedraw = false;
    }

    // Обновление часов
    // Planets обновляем чаще (50мс = 20fps) для плавного движения
    // Arcs достаточно раз в 250мс — они не анимированы покадрово
    if (currentMode == 0) {
        static unsigned long lastUpdate = 0;
        unsigned long interval = (display_get_skin() == SKIN_PLANETS) ? 50 : 250;
        if (millis() - lastUpdate > interval) {
            display_draw_clock();
            lastUpdate = millis();
        }
    }

    // Magic 8-Ball логика
    if (currentMode == 2 && mpu_ok) {
        static int shakeCounter = 0;
        static unsigned long lastShakeTime = 0;
        static unsigned long firstShakeTime = 0;

        if (millis() - lastShakeCheck > 100) {
            if (mpu_is_shaking()) {
                unsigned long now = millis();

                if (now - lastShakeTime > SHAKE_COOLDOWN_MS) {
                    if (shakeCounter == 0 || (now - firstShakeTime > SHAKE_TIMEOUT_MS)) {
                        shakeCounter = 1;
                        firstShakeTime = now;
                    } else {
                        shakeCounter++;
                    }
                    lastShakeTime = now;
                }
            }
            lastShakeCheck = millis();
        }

        if (shakeCounter >= SHAKE_DETECTION_COUNT) {
            display_show_prediction();
            shakeCounter = 0;
            needRedraw = true;
        }

        if (shakeCounter > 0 && (millis() - firstShakeTime > SHAKE_TIMEOUT_MS)) {
            shakeCounter = 0;
        }
    }

    // Обновление погоды
    if (millis() - lastWeatherUpdate > WEATHER_UPDATE_INTERVAL) {
        if (WiFi.status() == WL_CONNECTED) {
            update_weather();
            lastWeatherUpdate = millis();
            if (currentMode == 1) needRedraw = true;
        }
    }

    delay(20);
}