#include "weather.h"
#include "config.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

float  temperature = 0;
int    humidity    = 0;
int    weather_id  = 800;  // По умолчанию — ясно
String description = "";

void connect_wifi() {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    uint8_t cnt = 0;
    while (WiFi.status() != WL_CONNECTED && cnt++ < 30) delay(400);
}

void update_weather() {
    if (WiFi.status() != WL_CONNECTED) return;

    HTTPClient http;
    String url = "http://api.openweathermap.org/data/2.5/weather?q="
                 + String(CITY) + ",RU&units=metric&lang=en&appid=" + API_KEY;
    http.begin(url);
    int code = http.GET();

    if (code > 0) {
        String payload = http.getString();
        JsonDocument doc;
        deserializeJson(doc, payload);

        temperature = doc["main"]["temp"]              | 0.0f;
        humidity    = doc["main"]["humidity"]          | 0;
        weather_id  = doc["weather"][0]["id"]          | 800;
        description = doc["weather"][0]["description"].as<String>();
    }

    http.end();
}
