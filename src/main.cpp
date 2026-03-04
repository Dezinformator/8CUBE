#include <Arduino.h>
#include "config.h"
#include "display.h"
#include "mpu.h"
#include "weather.h"
#include "web.h"
#include <time.h>
#include <WiFi.h>
#include <math.h>
#include <esp_sntp.h>
#include <esp_timer.h>
#include <Preferences.h>

// ============================================================
// КОНСТАНТЫ И ДАННЫЕ
// ============================================================

const char* WIFI_SSID     = "SiriusAirusHaus";
const char* WIFI_PASSWORD = "Popova20";

// Координаты и ключ Яндекс Погоды — начальные значения.
// После первого сохранения через веб-интерфейс берутся из Preferences.
const char* YANDEX_WEATHER_KEY = "e8a70116-cc15-41b6-9caf-b210d290a3a4";
const char* WEATHER_LAT        = "54.7818";
const char* WEATHER_LON        = "32.0401";
const char* WEATHER_CITY       = "Smolensk";

const char* NTP_SERVER          = "time.google.com";
const long  GMT_OFFSET_SEC      = 10800;
const int   DAYLIGHT_OFFSET_SEC = 0;

const char* answers[] = {
    "Yes", "No", "Maybe", "No way", "Ask later",
    "Not now", "Likely", "Unlikely", "Certainly", "Never"
};
const int answersCount = 10;

// ============================================================
// ФЛАГИ — выставляются в таймере, читаются в loop()
// ============================================================

volatile bool flag_btn_short = false;
volatile bool flag_shaken    = false;

// ============================================================
// СОСТОЯНИЕ УСТРОЙСТВА
// ============================================================

bool mpu_ok        = false;
bool wifi_ok       = false;
bool time_synced   = false;
int  currentMode   = 0;
bool needRedraw    = true;

unsigned long lastWeatherUpdate    = 0;
unsigned long lastDiagnosticOutput = 0;
unsigned long lastNtpRetry         = 0;
unsigned long lastWifiRetry        = 0;   // последняя попытка переподключения WiFi
int           ntpAttempt           = 0;

// ============================================================
// ТАЙМЕР — каждые 5 мс
// ============================================================

static volatile int           btn_counter    = 0;
static volatile bool          btn_state      = false;
static volatile unsigned long btn_press_start = 0;
static volatile int           shake_count    = 0;
static volatile unsigned long shake_first_ms = 0;
static volatile unsigned long shake_last_ms  = 0;

// Счётчик тройного нажатия для смены скина (короткие)
static volatile int           triple_count    = 0;
static volatile unsigned long triple_last_ms  = 0;
#define TRIPLE_WINDOW_MS   1200
#define AP_HOLD_MS         5000   // удержание 5 сек → AP mode

volatile bool flag_triple  = false;
volatile bool flag_ap_mode = false;
bool ap_mode_active = false;


void IRAM_ATTR onTimer(void* arg) {
    unsigned long now_ms = (unsigned long)(esp_timer_get_time() / 1000ULL);

    // ── Антидребезг кнопки ────────────────────────────────────
    bool raw = (digitalRead(TOUCH_PIN) == HIGH);

    if (raw == btn_state) {
        btn_counter = 0;
    } else {
        btn_counter++;
        if (btn_counter >= 4) {
            // 4 одинаковых чтения подряд — переход подтверждён
            btn_state   = raw;
            btn_counter = 0;

            if (btn_state) {
                // Передний фронт — начало нажатия
                btn_press_start = now_ms;
            } else {
                // Задний фронт — конец нажатия
                unsigned long dur = now_ms - btn_press_start;
                if (dur > 50 && dur < (unsigned long)LONG_PRESS_MS) {
                    // Короткое нажатие — считаем для тройного
                    if (now_ms - triple_last_ms > TRIPLE_WINDOW_MS) {
                        triple_count = 1;
                    } else {
                        triple_count++;
                    }
                    triple_last_ms = now_ms;

                    if (triple_count >= 3) {
                        flag_triple  = true;
                        triple_count = 0;
                    } else {
                        flag_btn_short = true;
                    }
                }
            }
        }
    }

    // ── Длинное удержание ─────────────────────────────────────
    // > LONG_PRESS_MS (1.8с) — обычное длинное (для счётчика тройного)
    // > AP_HOLD_MS    (5с)   — AP mode, срабатывает один раз при достижении
    if (btn_state && btn_press_start > 0) {
        unsigned long held = now_ms - btn_press_start;

        if (held > AP_HOLD_MS && !flag_ap_mode) {
            // 5 секунд — переходим в AP mode
            flag_ap_mode    = true;
            btn_press_start = 0;  // сбрасываем чтобы не сработало повторно
            triple_count    = 0;
        }
    }

    // ── Тряска ────────────────────────────────────────────────
    if (mpu_ok && mpu_is_shaking()) {
        if (now_ms - shake_last_ms > (unsigned long)SHAKE_COOLDOWN_MS) {
            if (shake_count == 0 ||
                (now_ms - shake_first_ms > (unsigned long)SHAKE_TIMEOUT_MS)) {
                shake_count    = 1;
                shake_first_ms = now_ms;
            } else {
                shake_count++;
            }
            shake_last_ms = now_ms;
        }
    }
    if (shake_count > 0 &&
        (now_ms - shake_first_ms > (unsigned long)SHAKE_TIMEOUT_MS)) {
        shake_count = 0;
    }
    if (shake_count >= SHAKE_DETECTION_COUNT) {
        flag_shaken = true;
        shake_count = 0;
    }
}

// ============================================================
// ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
// ============================================================

void printDiagnostic() {
    Serial.println("\n=== STATUS ===");
    Serial.printf("Mode:     %s\n",
        currentMode == 0 ? "CLOCK" :
        currentMode == 1 ? "WEATHER" : "MAGIC BALL");
    Serial.printf("WiFi:     %s\n", wifi_ok    ? "OK" : "NO");
    Serial.printf("MPU:      %s\n", mpu_ok     ? "OK" : "NO");
    Serial.printf("NTP:      %s\n", time_synced ? "SYNCED" : "NO SYNC");
    Serial.println("=============\n");
}

void switchMode(int newMode) {
    currentMode = newMode % 3;
    display_clear();
    needRedraw = true;
    const char* names[] = { "CLOCK", "WEATHER", "MAGIC BALL" };
    Serial.printf("Mode: %s\n", names[currentMode]);
}

// Попытка синхронизации NTP — неблокирующая.
// Запускает configTime и возвращает сразу.
// Результат проверяется позже через sntp_get_sync_status().
void tryNtpSync() {
    if (WiFi.status() != WL_CONNECTED) return;
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
    ntpAttempt++;
    Serial.printf("NTP attempt #%d\n", ntpAttempt);
}

// Проверяет и обновляет статус NTP — вызывается в loop()
// Фоновое переподключение к WiFi — вызывается в loop()
void updateWifiStatus() {
    bool connected = (WiFi.status() == WL_CONNECTED);

    if (connected && !wifi_ok) {
        // Только что подключились (или восстановили соединение)
        wifi_ok = true;
        Serial.printf("WiFi connected: %s\n", WiFi.localIP().toString().c_str());
        static bool web_started = false;
        if (!web_started) {
            web_init();
            web_started = true;
        }
        // Сбрасываем NTP чтобы пересинхронизировать
        time_synced = false;
        ntpAttempt  = 0;
        display_set_time_status("NTP sync #1...");
        tryNtpSync();
        lastNtpRetry = millis();
        update_weather();
        lastWeatherUpdate = millis();
        if (currentMode == 1) needRedraw = true;
        return;
    }

    if (!connected) {
        if (wifi_ok) {
            // Только что потеряли соединение
            wifi_ok     = false;
            time_synced = false;
            Serial.println("WiFi: connection lost");
            display_set_time_status("No WiFi");
        }
        // Повторная попытка каждые 20 секунд
        if (millis() - lastWifiRetry > 20000) {
            Serial.println("WiFi: retrying...");
            WiFi.disconnect();
            delay(100);
            connect_wifi();
            lastWifiRetry = millis();
        }
    }
}

void updateNtpStatus() {
    if (time_synced) return;
    if (WiFi.status() != WL_CONNECTED) {
        display_set_time_status("No WiFi");
        return;
    }

    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
        time_synced = true;
        // Сбрасываем кэш часов чтобы сразу отрисовать время
        display_clear();
        needRedraw = true;
        struct tm t;
        if (getLocalTime(&t))
            Serial.printf("NTP synced: %02d:%02d:%02d\n",
                t.tm_hour, t.tm_min, t.tm_sec);
        return;
    }

    // Обновляем статус на экране
    char buf[32];
    snprintf(buf, sizeof(buf), "NTP sync #%d...", ntpAttempt);
    display_set_time_status(buf);

    // Повторная попытка каждые 15 секунд
    if (millis() - lastNtpRetry > 15000) {
        tryNtpSync();
        lastNtpRetry = millis();
    }
}

// ============================================================
// SETUP
// ============================================================

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n\n=== EIGHT CUBE STARTING ===");

    pinMode(TOUCH_PIN, INPUT);

    display_init();
    display_show_logo();
    delay(START_LOGO_DURATION_MS);

    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(100000);
    mpu_ok = mpu_init();

    // ── Проверяем флаг AP режима ──────────────────────────────
    // Читаем напрямую через Preferences — без вызовов web_* функций
    {
        Preferences p;
        p.begin("8cube", true);
        bool boot_ap = p.getBool("ap_mode", false);
        p.end();

        if (boot_ap) {
            Serial.println(">>> BOOT IN AP MODE <<<");

            // Сбрасываем флаг — следующий boot будет нормальным
            p.begin("8cube", false);
            p.putBool("ap_mode", false);
            p.end();

            // Полный сброс WiFi стека — стартуем с нуля
            WiFi.persistent(false);
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
            delay(500);

            // Поднимаем точку доступа
            WiFi.mode(WIFI_AP);
            delay(200);

            IPAddress ip(192,168,4,1);
            WiFi.softAPConfig(ip, ip, IPAddress(255,255,255,0));
            WiFi.softAP("8th-CUBE");  // открытая сеть
            delay(2000);  // DHCP серверу нужно время

            Serial.printf("AP IP: %s\n", WiFi.softAPIP().toString().c_str());

            // Запускаем веб-сервер и таймер кнопки
            web_init();

            const esp_timer_create_args_t ta = {
                .callback = &onTimer, .arg = nullptr,
                .dispatch_method = ESP_TIMER_TASK, .name = "input_poll"
            };
            esp_timer_handle_t th;
            esp_timer_create(&ta, &th);
            esp_timer_start_periodic(th, 5000);

            display_clear();
            display_show_ap_screen();
            ap_mode_active = true;
            return;
        }
    }

    // Выбор скина
    ClockSkin chosen = display_select_skin(SKIN_ARCS);
    display_set_skin(chosen);
    Serial.printf("Skin selected: %d\n", (int)chosen);

    // WiFi — первая попытка подключения.
    // Если не удалось — updateWifiStatus() в loop() будет повторять каждые 20 сек.
    display_set_time_status("WiFi...");
    connect_wifi();
    // Результат обработает updateWifiStatus() при первом вызове в loop()

    randomSeed(analogRead(0));

    // Таймер опроса входов — каждые 5 мс
    const esp_timer_create_args_t timer_args = {
        .callback        = &onTimer,
        .arg             = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name            = "input_poll"
    };
    esp_timer_handle_t timer_handle;
    esp_timer_create(&timer_args, &timer_handle);
    esp_timer_start_periodic(timer_handle, 5000);

    Serial.println("\n=== READY ===\n");

    display_clear();
    needRedraw    = true;
}

// ============================================================
// LOOP
// ============================================================

void loop() {

    // ── Диагностика раз в 30 сек ──────────────────────────────
    if (millis() - lastDiagnosticOutput > 30000) {
        printDiagnostic();
        lastDiagnosticOutput = millis();
    }

    // ── Фоновое переподключение WiFi и отслеживание NTP ──────
    updateWifiStatus();
    updateNtpStatus();

    // ── Три длинных нажатия → режим точки доступа (AP) ───────
    // Используется когда нет знакомой WiFi сети (в гостях и т.п.)
    // Кубик создаёт сеть "8th-CUBE", подключись и открой 192.168.4.1
    if (flag_ap_mode) {
        flag_ap_mode = false;
        Serial.println(">>> SAVING AP FLAG — restarting <<<");
        // Сохраняем флаг напрямую — без web_ функций
        Preferences p;
        p.begin("8cube", false);
        p.putBool("ap_mode", true);
        p.end();
        delay(200);
        ESP.restart();
    }

    // Пока активен AP режим — ждём только кнопку для выхода
    if (ap_mode_active) {
        if (flag_btn_short) {
            // Короткое нажатие → перезагрузка в нормальный режим
            Serial.println(">>> EXIT AP MODE — restarting <<<");
            ESP.restart();
        }
        delay(10);
        return;
    }

    // ── Тройное нажатие → смена скина ─────────────────────────
    if (flag_triple) {
        flag_triple = false;
        Serial.println(">>> SELECT SKIN <<<");
        ClockSkin chosen = display_select_skin(display_get_skin());
        display_set_skin(chosen);
        Serial.printf("Skin changed: %d\n", (int)chosen);
        display_clear();
        needRedraw = true;
    }

    // ── Команды от веб-интерфейса ─────────────────────────────
    if (web_flag_mode_changed) {
        web_flag_mode_changed = false;
        switchMode(web_new_mode);
        Serial.printf("Web: mode → %d\n", web_new_mode);
    }

    if (web_flag_skin_changed) {
        web_flag_skin_changed = false;
        display_set_skin((ClockSkin)web_new_skin);
        display_clear();
        needRedraw = true;
        Serial.printf("Web: skin → %d\n", web_new_skin);
    }

    if (web_flag_settings_saved) {
        web_flag_settings_saved = false;
        // Применяем новые настройки погоды и часового пояса
        configTime(web_get_tz() * 3600L, 0, NTP_SERVER);
        update_weather();
        lastWeatherUpdate = millis();
        if (currentMode == 1) needRedraw = true;
        Serial.println("Web: settings applied");
    }

    // ── Короткое нажатие → смена режима ──────────────────────
    if (flag_btn_short) {
        flag_btn_short = false;
        switchMode(currentMode + 1);
    }

    // ── Первичная отрисовка при смене режима ──────────────────
    if (needRedraw) {
        switch (currentMode) {
            case 0: display_draw_clock();      break;
            case 1: display_draw_weather();    break;
            case 2: display_draw_magic_ball(); break;
        }
        needRedraw = false;
    }

    // ── Обновление часов ──────────────────────────────────────
    if (currentMode == 0) {
        static unsigned long lastClockUpdate = 0;
        unsigned long interval = (display_get_skin() == SKIN_PLANETS) ? 50 : 250;
        if (millis() - lastClockUpdate > interval) {
            display_draw_clock();
            lastClockUpdate = millis();
        }
    }

    // ── Тряска → предсказание (Magic Ball) ───────────────────
    if (currentMode == 2 && flag_shaken) {
        flag_shaken = false;
        display_show_prediction();
        needRedraw = true;
    }

    // ── Обновление погоды ─────────────────────────────────────
    if (millis() - lastWeatherUpdate > WEATHER_UPDATE_INTERVAL) {
        if (WiFi.status() == WL_CONNECTED) {
            update_weather();
            lastWeatherUpdate = millis();
            if (currentMode == 1) needRedraw = true;
        }
    }

    delay(10);
}