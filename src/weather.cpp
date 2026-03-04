#include "weather.h"
#include "config.h"
#include "web.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>

float  temperature = 0;
int    humidity    = 0;
int    weather_id  = WX_SUNNY;
String description = "";

void connect_wifi() {
    Preferences p;

    // Собираем список всех сетей: сначала сохранённые, потом хардкод как запасная
    struct Network { String ssid; String pwd; };
    Network nets[6];
    int total = 0;

    // Сохранённые сети из Preferences
    p.begin("8cube", true);
    int count = p.getInt("wifi_count", 0);
    for (int i = 0; i < count && total < 5; i++) {
        String s = p.getString(("wssid" + String(i)).c_str(), "");
        String pw = p.getString(("wpwd"  + String(i)).c_str(), "");
        if (s.length() > 0) {
            nets[total++] = {s, pw};
        }
    }
    p.end();

    // Хардкод — всегда добавляем если его ещё нет в списке
    bool found = false;
    for (int i = 0; i < total; i++) {
        if (nets[i].ssid == String(WIFI_SSID)) { found = true; break; }
    }
    if (!found && total < 6) {
        nets[total++] = {String(WIFI_SSID), String(WIFI_PASSWORD)};
    }

    if (total == 0) return;

    // Перебираем все сети
    for (int i = 0; i < total; i++) {
        Serial.printf("WiFi: trying [%d/%d] %s\n", i+1, total, nets[i].ssid.c_str());
        WiFi.begin(nets[i].ssid.c_str(), nets[i].pwd.c_str());

        uint8_t cnt = 0;
        while (WiFi.status() != WL_CONNECTED && cnt++ < 20) delay(400);

        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("WiFi: connected to %s  IP: %s\n",
                nets[i].ssid.c_str(), WiFi.localIP().toString().c_str());
            return;
        }
        Serial.printf("WiFi: no response from %s\n", nets[i].ssid.c_str());
        WiFi.disconnect();
        delay(200);
    }
    Serial.println("WiFi: all networks failed");
}

// ============================================================
// Маппинг condition-кодов Яндекс Погоды → индекс иконки
//
// Полный список condition из документации Яндекса:
// clear, partly-cloudy, cloudy, overcast,
// light-rain, rain, heavy-rain, showers, wet-snow,
// light-snow, snow, snow-showers,
// hail, thunderstorm, thunderstorm-with-rain, thunderstorm-with-hail
// ============================================================
static int conditionToIcon(const char* cond) {
    if (strcmp(cond, "clear") == 0)
        return WX_SUNNY;

    if (strcmp(cond, "partly-cloudy") == 0)
        return WX_FEW_CLOUDS;

    if (strcmp(cond, "cloudy") == 0)
        return WX_SCATTERED;

    if (strcmp(cond, "overcast") == 0)
        return WX_OVERCAST;

    if (strcmp(cond, "light-rain")  == 0 ||
        strcmp(cond, "rain")        == 0 ||
        strcmp(cond, "heavy-rain")  == 0 ||
        strcmp(cond, "showers")     == 0 ||
        strcmp(cond, "wet-snow")    == 0)   // мокрый снег → дождь
        return WX_RAIN;

    if (strcmp(cond, "light-snow")   == 0 ||
        strcmp(cond, "snow")         == 0 ||
        strcmp(cond, "snow-showers") == 0 ||
        strcmp(cond, "hail")         == 0)
        return WX_SNOW;

    if (strcmp(cond, "thunderstorm")            == 0 ||
        strcmp(cond, "thunderstorm-with-rain")  == 0 ||
        strcmp(cond, "thunderstorm-with-hail")  == 0)
        return WX_THUNDER;

    // Всё неизвестное → пасмурно
    return WX_OVERCAST;
}

void update_weather() {
    if (WiFi.status() != WL_CONNECTED) return;

    HTTPClient http;

    // Координаты берём из веб-модуля — там они актуальны
    // (либо начальные из config, либо обновлённые через веб-интерфейс)
    String url = "https://api.weather.yandex.ru/v2/forecast?lat="
                 + web_get_lat() + "&lon=" + web_get_lon()
                 + "&limit=1&hours=false&extra=false";

    http.begin(url);
    http.addHeader("X-Yandex-Weather-Key", YANDEX_WEATHER_KEY);

    int code = http.GET();

    if (code == 200) {
        String payload = http.getString();
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);

        if (!err) {
            // fact — текущие фактические данные
            JsonObject fact = doc["fact"];

            temperature = fact["temp"]     | 0.0f;
            humidity    = fact["humidity"] | 0;

            // condition — строковый код, конвертируем в индекс иконки
            const char* cond = fact["condition"] | "clear";
            weather_id = conditionToIcon(cond);

            // Человекочитаемое описание берём из condition
            // (Яндекс не даёт отдельного description)
            description = String(cond);

            Serial.printf("Weather OK: %.1f°C  %d%%  %s (icon %d)\n",
                temperature, humidity, cond, weather_id);
        } else {
            Serial.printf("Weather JSON error: %s\n", err.c_str());
        }
    } else {
        Serial.printf("Weather HTTP error: %d\n", code);
    }

    http.end();
}