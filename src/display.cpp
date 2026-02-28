#include "display.h"
#include "weather.h"
#include "config.h"
#include "mpu.h"                 // ← для mpu_read_accel на экране выбора скина
#include "weather_icons.h"   // ← Пиксель-арт иконки погоды (PROGMEM)
#include <Arduino.h>
#include <math.h>
#include <time.h>
#include <WiFi.h>

// Глобальный объект дисплея
Adafruit_GC9A01A tft(TFT_CS, TFT_DC, TFT_RST);

// Forward declaration — функция определена ниже, но используется раньше
static uint16_t blendColor565(uint16_t a, uint16_t b, float t);

// ============================================================
// КЭШ ДЛЯ ОПТИМИЗАЦИИ ПЕРЕРИСОВКИ
// ============================================================

static int last_min = -1;
static int last_hour = -1;
static int last_sec = -1;

static int last_date_day = -1;
static int last_date_month = -1;
static int last_date_year = -1;

static float last_sec_progress = 0.0f;
static float last_min_progress = 0.0f;
static float last_hour_progress = 0.0f;

// Кэш позиций планет (скин PLANETS)
static float last_planet_sec_angle  = -999.0f;
static float last_planet_min_angle  = -999.0f;
static float last_planet_hour_angle = -999.0f;

// Флаги однократной инициализации скина PLANETS
static bool planets_stars_drawn  = false;
static bool planets_orbits_drawn = false;
static bool planets_calibrated   = false;
static unsigned long planets_sec_off  = 0;
static unsigned long planets_min_off  = 0;
static unsigned long planets_hour_off = 0;

// ============================================================
// ВСПОМОГАТЕЛЬНАЯ ФУНКЦИЯ: рисуем иконку из PROGMEM
// x, y — верхний левый угол иконки
// icon — массив uint16_t в PROGMEM (64x64 пикселей)
// w, h — размер иконки (64, 64)
// ============================================================
static void drawIcon(int x, int y, const uint16_t* icon, int w, int h) {
    // Читаем данные из Flash построчно и рисуем через drawRGBBitmap
    // Но сначала копируем строку в буфер (RAM), так как drawRGBBitmap
    // требует указатель в RAM на некоторых версиях библиотеки
    uint16_t lineBuf[64];  // буфер одной строки (64 пикселя × 2 байта = 128 байт)

    for (int row = 0; row < h; row++) {
        // Копируем одну строку из Flash в RAM
        for (int col = 0; col < w; col++) {
            lineBuf[col] = pgm_read_word(&icon[row * w + col]);
        }
        // Рисуем строку пикселей
        // Пиксели с цветом 0x0000 (чёрный) — "прозрачные" (фон у нас тоже чёрный)
        tft.drawRGBBitmap(x, y + row, lineBuf, w, 1);
    }
}

// ============================================================
// ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ДЛЯ ЭКРАНА ЧАСОВ
// ============================================================

void drawArcSegment(uint16_t color, float prev_progress, float curr_progress, int radius, int thickness) {
    if (curr_progress <= prev_progress) return;

    const int centerX = 120;
    const int centerY = 120;

    float startAngleDeg = -90.0f + (prev_progress * 360.0f);
    float endAngleDeg   = -90.0f + (curr_progress * 360.0f);
    float stepDeg = 1.0f;

    int draw_thickness = (color == GC9A01A_BLACK) ? thickness + 4 : thickness;

    int r_outer = radius + draw_thickness / 2;
    int r_inner = radius - draw_thickness / 2;
    if (r_inner < 0) r_inner = 0;

    for (float angle = startAngleDeg; angle < endAngleDeg; angle += stepDeg) {
        float rad1 = angle * PI / 180.0f;
        float rad2 = (angle + stepDeg) * PI / 180.0f;

        int x1_outer = centerX + r_outer * cos(rad1);
        int y1_outer = centerY + r_outer * sin(rad1);
        int x2_outer = centerX + r_outer * cos(rad2);
        int y2_outer = centerY + r_outer * sin(rad2);

        int x1_inner = centerX + r_inner * cos(rad1);
        int y1_inner = centerY + r_inner * sin(rad1);
        int x2_inner = centerX + r_inner * cos(rad2);
        int y2_inner = centerY + r_inner * sin(rad2);

        tft.fillTriangle(x1_inner, y1_inner, x1_outer, y1_outer, x2_outer, y2_outer, color);
        tft.fillTriangle(x1_inner, y1_inner, x2_outer, y2_outer, x2_inner, y2_inner, color);
    }
}

// ============================================================
// ИНИЦИАЛИЗАЦИЯ ДИСПЛЕЯ
// ============================================================

void display_init() {
    tft.begin(4000000);
    tft.setRotation(0);
    tft.fillScreen(GC9A01A_BLACK);
    tft.setTextColor(GC9A01A_WHITE, GC9A01A_BLACK);

    last_min = -1;
    last_hour = -1;
    last_sec = -1;
    last_date_day = -1;
    last_date_month = -1;
    last_date_year = -1;
    last_sec_progress = 0.0f;
    last_min_progress = 0.0f;
    last_hour_progress = 0.0f;
}

// ============================================================
// ЛОГОТИП
// ============================================================

void display_show_logo() {
    tft.fillScreen(GC9A01A_BLACK);
    tft.fillRect(60, 30, 120, 120, GC9A01A_WHITE);

    tft.setTextColor(GC9A01A_BLACK);
    tft.setTextSize(12);
    tft.setCursor(70, 40);
    tft.print("8");

    tft.setTextColor(GC9A01A_BLACK);
    tft.setTextSize(3);
    tft.setCursor(140, 40);
    tft.print("th");

    tft.setTextColor(GC9A01A_WHITE);
    tft.setTextSize(3);
    tft.setCursor(90, 170);
    tft.print("CUBE");
}

// ============================================================
// ОЧИСТКА ЭКРАНА
// ============================================================

void display_clear() {
    tft.fillScreen(GC9A01A_BLACK);

    last_min = -1;
    last_hour = -1;
    last_sec = -1;
    last_date_day = -1;
    last_date_month = -1;
    last_date_year = -1;
    last_sec_progress = 0.0f;
    last_min_progress = 0.0f;
    last_hour_progress = 0.0f;

    // Сброс кэша планет — при смене скина или пробуждении
    // орбиты перерисуются заново
    last_planet_sec_angle  = -999.0f;
    last_planet_min_angle  = -999.0f;
    last_planet_hour_angle = -999.0f;

    // Сброс флагов скина PLANETS — звёзды и орбиты перерисуются,
    // калибровка времени пересчитается при следующем входе
    planets_stars_drawn  = false;
    planets_orbits_drawn = false;
    planets_calibrated   = false;
}

// ============================================================
// АКТИВНЫЙ СКИН — устанавливается из main.cpp
// ============================================================

static ClockSkin activeSkin = SKIN_ARCS;

void display_set_skin(ClockSkin skin) {
    activeSkin = skin;
}

ClockSkin display_get_skin() {
    return activeSkin;
}

// ============================================================
// СКИН 1: ARCS — дуги прогресса (палитра ГАЛАКТИКА)
// Дуги: Секундная cyan r=110 t=4, Минутная green r=100 t=6,
//        Часовая magenta r=88 t=8
// ============================================================

static void clock_draw_arcs() {
    struct tm timeinfo;

    if (!getLocalTime(&timeinfo)) {
        tft.fillRect(0, 0, 240, 240, GC9A01A_BLACK);
        tft.setTextColor(GC9A01A_RED);
        tft.setCursor(40, 100);
        tft.setTextSize(2);
        tft.println("No time");
        return;
    }

    float sec_progress  = timeinfo.tm_sec / 60.0f;
    float min_progress  = (timeinfo.tm_min + sec_progress / 60.0f) / 60.0f;
    float hour_progress = (timeinfo.tm_hour % 12 + min_progress / 12.0f) / 12.0f;

    // Сброс дуг при новом круге
    if (sec_progress < last_sec_progress) {
        drawArcSegment(GC9A01A_BLACK, 0.0f, 1.0f, 110, 4);
        last_sec_progress = 0.0f;
    }
    if (min_progress < last_min_progress) {
        drawArcSegment(GC9A01A_BLACK, 0.0f, 1.0f, 100, 6);
        last_min_progress = 0.0f;
    }
    if (hour_progress < last_hour_progress) {
        drawArcSegment(GC9A01A_BLACK, 0.0f, 1.0f, 88, 8);
        last_hour_progress = 0.0f;
    }

    // Дорисовка новых сегментов дуг
    if (sec_progress > last_sec_progress) {
        drawArcSegment(GC9A01A_CYAN,    last_sec_progress,  sec_progress,  110, 4);
        last_sec_progress = sec_progress;
    }
    if (min_progress > last_min_progress) {
        drawArcSegment(GC9A01A_GREEN,   last_min_progress,  min_progress,  100, 6);
        last_min_progress = min_progress;
    }
    if (hour_progress > last_hour_progress) {
        drawArcSegment(GC9A01A_MAGENTA, last_hour_progress, hour_progress, 88,  8);
        last_hour_progress = hour_progress;
    }

    char buf[10];

    // Часы (MAGENTA)
    if (timeinfo.tm_hour != last_hour) {
        tft.fillRect(55, 80, 55, 40, GC9A01A_BLACK);
        strftime(buf, sizeof(buf), "%H", &timeinfo);
        tft.setTextColor(GC9A01A_MAGENTA);
        tft.setTextSize(5);
        tft.setCursor(55, 80);
        tft.print(buf);
        last_hour = timeinfo.tm_hour;
    }

    // Двоеточие (CYAN) — мигает каждую секунду
    tft.fillRect(110, 80, 20, 40, GC9A01A_BLACK);
    tft.setTextColor(GC9A01A_CYAN);
    tft.setTextSize(5);
    tft.setCursor(110, 80);
    tft.print((timeinfo.tm_sec % 2 == 0) ? ":" : " ");
    last_sec = timeinfo.tm_sec;

    // Минуты (GREEN)
    if (timeinfo.tm_min != last_min) {
        tft.fillRect(130, 80, 55, 40, GC9A01A_BLACK);
        strftime(buf, sizeof(buf), "%M", &timeinfo);
        tft.setTextColor(GC9A01A_GREEN);
        tft.setTextSize(5);
        tft.setCursor(130, 80);
        tft.print(buf);
        last_min = timeinfo.tm_min;
    }

    // Дата (WHITE)
    if (timeinfo.tm_mday != last_date_day ||
        timeinfo.tm_mon  != last_date_month ||
        timeinfo.tm_year != last_date_year) {

        tft.fillRect(50, 150, 140, 20, GC9A01A_BLACK);
        strftime(buf, sizeof(buf), "%d.%m.%y", &timeinfo);
        tft.setTextColor(GC9A01A_WHITE);
        tft.setTextSize(2);

        int textWidth = strlen(buf) * 6 * 2;
        int xPos = 120 - (textWidth / 2);
        if (xPos < 10) xPos = 10;

        tft.setCursor(xPos, 155);
        tft.print(buf);

        last_date_day   = timeinfo.tm_mday;
        last_date_month = timeinfo.tm_mon;
        last_date_year  = timeinfo.tm_year;
    }
}

// ============================================================
// СКИН 2: PLANETS — планеты на орбитах
//
// Три точки летят по круговым орбитам:
//   Секунды — маленькая яркая точка, внешняя орбита r=108
//   Минуты  — средняя точка,         средняя орбита  r=88
//   Часы    — большая точка,          внутренняя орбита r=66
//
// За каждой планетой тянется хвост (след из затухающих точек).
// В центре — цифры ЧЧ:ММ и дата, как в скине ARCS.
// ============================================================

// Рисует планету с хвостом на орбите.
// angle — текущий угол в радианах (0 = верх, по часовой)
// r_orbit — радиус орбиты
// planet_r — радиус точки-планеты
// color — цвет планеты
// tail_steps — количество точек хвоста
// step_angle — угловой шаг хвоста (радианы)
static void drawPlanet(float angle, int r_orbit, int planet_r,
                       uint16_t color, int tail_steps, float step_angle) {
    const int cx = 120, cy = 120;

    // Рисуем хвост от старого к новому (дальний хвост тусклее)
    for (int i = tail_steps; i >= 1; i--) {
        float tail_angle = angle - i * step_angle;
        int tx = cx + (int)(r_orbit * sin(tail_angle));
        int ty = cy - (int)(r_orbit * cos(tail_angle));
        float fade = 1.0f - (float)i / (tail_steps + 1);
        uint16_t tail_color = blendColor565(GC9A01A_BLACK, color, fade * 0.45f);
        tft.fillCircle(tx, ty, planet_r - 1, tail_color);
    }

    // Рисуем планету
    int px = cx + (int)(r_orbit * sin(angle));
    int py = cy - (int)(r_orbit * cos(angle));
    tft.fillCircle(px, py, planet_r, color);

    // Блик
    if (planet_r >= 3) {
        uint16_t highlight = blendColor565(color, GC9A01A_WHITE, 0.35f);
        tft.fillCircle(px - 1, py - 1, planet_r / 2, highlight);
    }
}

// Стирает только самый дальний хвостовой пиксель и восстанавливает орбиту под ним.
// Вызывается ПЕРЕД drawPlanet с предыдущим углом.
// Это намного быстрее чем стирать всё и перерисовывать.
static void erasePlanetTail(float old_angle, int r_orbit, int planet_r,
                            int tail_steps, float step_angle, uint16_t orbitColor) {
    const int cx = 120, cy = 120;
    int erase_r = planet_r;  // без запаса — точно по размеру

    // Стираем только самую дальнюю точку хвоста (остальные перекроет новый хвост)
    float tail_end_angle = old_angle - tail_steps * step_angle;
    int tx = cx + (int)(r_orbit * sin(tail_end_angle));
    int ty = cy - (int)(r_orbit * cos(tail_end_angle));
    tft.fillCircle(tx, ty, erase_r, GC9A01A_BLACK);

    // Восстанавливаем орбиту в том месте где стёрли — только нужные пиксели
    // Рисуем маленький дуговой патч вместо полного круга
    for (float a = tail_end_angle - 0.08f; a <= tail_end_angle + 0.08f; a += 0.02f) {
        int ox = cx + (int)(r_orbit * sin(a));
        int oy = cy - (int)(r_orbit * cos(a));
        tft.drawPixel(ox, oy, orbitColor);
    }

    // Стираем также старую голову планеты (она сдвинулась)
    float head_angle = old_angle;
    int hx = cx + (int)(r_orbit * sin(head_angle));
    int hy = cy - (int)(r_orbit * cos(head_angle));
    tft.fillCircle(hx, hy, erase_r + 1, GC9A01A_BLACK);

    // Восстанавливаем орбиту под старой головой
    for (float a = head_angle - 0.12f; a <= head_angle + 0.12f; a += 0.02f) {
        int ox = cx + (int)(r_orbit * sin(a));
        int oy = cy - (int)(r_orbit * cos(a));
        tft.drawPixel(ox, oy, orbitColor);
    }
}

static void clock_draw_planets() {
    struct tm timeinfo;

    if (!getLocalTime(&timeinfo)) {
        tft.fillRect(0, 0, 240, 240, GC9A01A_BLACK);
        tft.setTextColor(GC9A01A_RED);
        tft.setCursor(40, 100);
        tft.setTextSize(2);
        tft.println("No time");
        return;
    }

    // ── Параметры орбит ────────────────────────────────────────
    const int   SEC_R    = 108;  const int SEC_PR   = 4;
    const int   SEC_TAIL = 6;    const float SEC_STEP = 0.10f;

    const int   MIN_R    = 88;   const int MIN_PR   = 5;
    const int   MIN_TAIL = 4;    const float MIN_STEP = 0.07f;

    const int   HOUR_R   = 66;   const int HOUR_PR  = 7;
    const int   HOUR_TAIL= 3;    const float HOUR_STEP = 0.06f;

    const uint16_t ORBIT_COLOR = 0x18C3;

    // ── Звёзды — рисуем один раз при старте ───────────────────
    // 38 точек вне орбит и вне зоны цифр (центр экрана)
    struct Star { uint8_t x, y, r; };
    static const Star stars[] = {
        {20,55,1},{45,30,1},{70,18,2},{110,12,1},{145,22,1},
        {175,40,2},{200,25,1},{220,60,1},{228,90,1},{225,125,1},
        {218,158,2},{205,185,1},{185,208,1},{155,220,2},{120,228,1},
        {88,222,1},{58,210,2},{35,192,1},{18,168,1},{12,135,1},
        {15,100,2},{22,72,1},{38,48,1},{160,15,1},{195,55,2},
        {215,95,1},{210,140,2},{190,175,1},{165,200,1},{130,215,2},
        {95,210,1},{62,195,2},{40,170,1},{25,142,1},{30,78,2},
        {52,52,1},{82,28,2},{178,32,1},
    };
    static const int STAR_COUNT = 38;

    static bool blinkOff = false;
    static int  blinkIdx = 0;
    static unsigned long lastBlink = 0;

    if (!planets_stars_drawn) {
        for (int i = 0; i < STAR_COUNT; i++) {
            tft.fillCircle(stars[i].x, stars[i].y, stars[i].r, GC9A01A_WHITE);
        }
        planets_stars_drawn = true;
    }

    // ── Мерцание: каждые 600мс одна звезда гаснет/загорается ──
    if (millis() - lastBlink > 1800) {
        // Восстанавливаем предыдущую
        tft.fillCircle(stars[blinkIdx].x, stars[blinkIdx].y,
                       stars[blinkIdx].r, GC9A01A_WHITE);
        // Выбираем следующую (детерминированно)
        blinkIdx = (blinkIdx + 7) % STAR_COUNT;
        blinkOff = !blinkOff;
        uint16_t c = blinkOff ? GC9A01A_BLACK : GC9A01A_WHITE;
        tft.fillCircle(stars[blinkIdx].x, stars[blinkIdx].y,
                       stars[blinkIdx].r, c);
        lastBlink = millis();
    }

    // ── Орбиты — один раз при старте ──────────────────────────
    if (!planets_orbits_drawn) {
        tft.drawCircle(120, 120, SEC_R,  ORBIT_COLOR);
        tft.drawCircle(120, 120, MIN_R,  ORBIT_COLOR);
        tft.drawCircle(120, 120, HOUR_R, ORBIT_COLOR);
        planets_orbits_drawn = true;
    }

    // ── Углы: millis() + offset от реального времени ──────────
    // При первом вызове один раз вычисляем offset так, чтобы
    // планеты стартовали с позиции соответствующей текущему времени,
    // а дальше двигались плавно через millis() без скачков.
    if (!planets_calibrated) {
        unsigned long real_sec  = (unsigned long)timeinfo.tm_sec * 1000UL;
        unsigned long real_min  = ((unsigned long)timeinfo.tm_min  * 60UL
                                 + (unsigned long)timeinfo.tm_sec) * 1000UL;
        unsigned long real_hour = (((unsigned long)(timeinfo.tm_hour % 12) * 3600UL)
                                 + ((unsigned long)timeinfo.tm_min  * 60UL)
                                 + (unsigned long)timeinfo.tm_sec) * 1000UL;

        unsigned long ms0 = millis();
        planets_sec_off  = (60000UL       + real_sec  - ms0 % 60000UL)    % 60000UL;
        planets_min_off  = (3600000UL     + real_min  - ms0 % 3600000UL)  % 3600000UL;
        planets_hour_off = (43200000UL    + real_hour - ms0 % 43200000UL) % 43200000UL;

        planets_calibrated = true;
        Serial.printf("Planets calibrated: %02d:%02d:%02d\n",
            timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    }

    unsigned long ms = millis();
    float sec_angle  = (float)((ms + planets_sec_off)  % 60000UL)    / 60000.0f    * 2.0f * PI;
    float min_angle  = (float)((ms + planets_min_off)  % 3600000UL)  / 3600000.0f  * 2.0f * PI;
    float hour_angle = (float)((ms + planets_hour_off) % 43200000UL) / 43200000.0f * 2.0f * PI;

    // ── Стираем хвосты точечно ────────────────────────────────
    if (last_planet_sec_angle > -900.0f) {
        erasePlanetTail(last_planet_sec_angle,  SEC_R,  SEC_PR,  SEC_TAIL,  SEC_STEP,  ORBIT_COLOR);
        erasePlanetTail(last_planet_min_angle,  MIN_R,  MIN_PR,  MIN_TAIL,  MIN_STEP,  ORBIT_COLOR);
        erasePlanetTail(last_planet_hour_angle, HOUR_R, HOUR_PR, HOUR_TAIL, HOUR_STEP, ORBIT_COLOR);
    }

    // ── Рисуем планеты ────────────────────────────────────────
    drawPlanet(hour_angle, HOUR_R, HOUR_PR, GC9A01A_MAGENTA, HOUR_TAIL, HOUR_STEP);
    drawPlanet(min_angle,  MIN_R,  MIN_PR,  GC9A01A_GREEN,   MIN_TAIL,  MIN_STEP);
    drawPlanet(sec_angle,  SEC_R,  SEC_PR,  GC9A01A_CYAN,    SEC_TAIL,  SEC_STEP);

    last_planet_sec_angle  = sec_angle;
    last_planet_min_angle  = min_angle;
    last_planet_hour_angle = hour_angle;

    // ── Цифры в центре (ЧЧ:ММ) ────────────────────────────────
    char buf[10];
    int cur_sec = timeinfo.tm_sec;

    if (timeinfo.tm_hour != last_hour) {
        tft.fillRect(60, 92, 50, 32, GC9A01A_BLACK);
        strftime(buf, sizeof(buf), "%H", &timeinfo);
        tft.setTextColor(GC9A01A_MAGENTA);
        tft.setTextSize(4);
        tft.setCursor(60, 92);
        tft.print(buf);
        last_hour = timeinfo.tm_hour;
    }

    // Двоеточие мигает
    tft.fillRect(107, 92, 14, 32, GC9A01A_BLACK);
    tft.setTextColor(GC9A01A_CYAN);
    tft.setTextSize(4);
    tft.setCursor(107, 92);
    tft.print((cur_sec % 2 == 0) ? ":" : " ");
    last_sec = cur_sec;

    if (timeinfo.tm_min != last_min) {
        tft.fillRect(121, 92, 50, 32, GC9A01A_BLACK);
        strftime(buf, sizeof(buf), "%M", &timeinfo);
        tft.setTextColor(GC9A01A_GREEN);
        tft.setTextSize(4);
        tft.setCursor(121, 92);
        tft.print(buf);
        last_min = timeinfo.tm_min;
    }

    // ── Дата ──────────────────────────────────────────────────
    if (timeinfo.tm_mday != last_date_day ||
        timeinfo.tm_mon  != last_date_month ||
        timeinfo.tm_year != last_date_year) {

        tft.fillRect(50, 138, 140, 16, GC9A01A_BLACK);
        strftime(buf, sizeof(buf), "%d.%m.%y", &timeinfo);
        tft.setTextColor(GC9A01A_WHITE);
        tft.setTextSize(2);
        int textWidth = strlen(buf) * 6 * 2;
        tft.setCursor(120 - textWidth / 2, 138);
        tft.print(buf);

        last_date_day   = timeinfo.tm_mday;
        last_date_month = timeinfo.tm_mon;
        last_date_year  = timeinfo.tm_year;
    }
}

// ============================================================
// ДИСПЕТЧЕР ЧАСОВ — вызывает нужный скин
// ============================================================

void display_draw_clock() {
    switch (activeSkin) {
        case SKIN_PLANETS: clock_draw_planets(); break;
        case SKIN_ARCS:
        default:           clock_draw_arcs();    break;
    }
}

// ============================================================
// ЭКРАН ВЫБОРА СКИНА
// Вызывается один раз при старте, после логотипа.
// Управление: наклон влево/вправо — листать, короткое нажатие — выбрать.
// Возвращает выбранный ClockSkin.
// ============================================================

// Рисует превью одного скина на экране выбора
static void drawSkinPreview(ClockSkin skin, bool selected) {
    tft.fillScreen(GC9A01A_BLACK);

    const int cx = 120, cy = 105;

    if (skin == SKIN_ARCS) {
        // Превью ARCS: три незамкнутые дуги + цифры
        // Рисуем статичную картинку — 75% каждой дуги для наглядности
        uint16_t colors[3]   = { GC9A01A_CYAN, GC9A01A_GREEN, GC9A01A_MAGENTA };
        int      radii[3]    = { 80, 68, 55 };
        int      thickness[3]= { 4, 6, 8 };

        for (int i = 0; i < 3; i++) {
            float end = 0.75f - i * 0.15f;  // у каждой дуги разный прогресс
            for (float a = -90.0f; a < -90.0f + end * 360.0f; a += 1.5f) {
                float rad = a * PI / 180.0f;
                int r_o = radii[i] + thickness[i] / 2;
                int r_i = radii[i] - thickness[i] / 2;
                int x1 = cx + r_o * cos(rad), y1 = cy + r_o * sin(rad);
                int x2 = cx + r_i * cos(rad), y2 = cy + r_i * sin(rad);
                tft.drawLine(x1, y1, x2, y2, colors[i]);
            }
        }
        // Цифры-заглушка по центру
        tft.setTextColor(GC9A01A_MAGENTA);
        tft.setTextSize(3);
        tft.setCursor(cx - 33, cy - 16);
        tft.print("12");
        tft.setTextColor(GC9A01A_CYAN);
        tft.print(":");
        tft.setTextColor(GC9A01A_GREEN);
        tft.print("00");

    } else {
        // Превью PLANETS: три орбиты с точками
        uint16_t orbitColor = 0x2104;
        tft.drawCircle(cx, cy, 80, orbitColor);
        tft.drawCircle(cx, cy, 62, orbitColor);
        tft.drawCircle(cx, cy, 44, orbitColor);

        // Планеты в красивых позициях
        tft.fillCircle(cx + 80,  cy,      3, GC9A01A_CYAN);
        tft.fillCircle(cx - 44,  cy - 44, 4, GC9A01A_GREEN);
        tft.fillCircle(cx + 31,  cy - 31, 5, GC9A01A_MAGENTA);

        // Хвостики
        for (int t = 1; t <= 5; t++) {
            float a = t * 0.1f;
            int tx = cx + (int)(80 * cos(-a));
            int ty = cy + (int)(80 * sin(-a));
            uint16_t tc = blendColor565(GC9A01A_BLACK, GC9A01A_CYAN, 0.4f - t * 0.06f);
            tft.fillCircle(tx, ty, 2, tc);
        }

        // Цифры в центре
        tft.setTextColor(GC9A01A_MAGENTA);
        tft.setTextSize(3);
        tft.setCursor(cx - 33, cy - 16);
        tft.print("12");
        tft.setTextColor(GC9A01A_CYAN);
        tft.print(":");
        tft.setTextColor(GC9A01A_GREEN);
        tft.print("00");
    }

    // ── Название скина ─────────────────────────────────────────
    const char* names[SKIN_COUNT] = { "ARCS", "PLANETS" };
    uint16_t nameColor = selected ? GC9A01A_WHITE : 0x7BEF;

    tft.setTextSize(2);
    tft.setTextColor(nameColor);
    const char* name = names[(int)skin];
    int nw = strlen(name) * 12;
    tft.setCursor(120 - nw / 2, 172);
    tft.print(name);

    // ── Стрелки навигации ──────────────────────────────────────
    tft.setTextColor(0x4208);  // тёмно-серый
    tft.setTextSize(2);
    tft.setCursor(24, 172);
    tft.print("<");
    tft.setCursor(204, 172);
    tft.print(">");

    // ── Подсказка снизу ────────────────────────────────────────
    tft.setTextSize(1);
    tft.setTextColor(0x4208);
    const char* hint = "tilt to browse  tap to select";
    tft.setCursor(120 - (strlen(hint) * 6 / 2), 200);
    tft.print(hint);

    // ── Рамка вокруг превью если выбран ───────────────────────
    if (selected) {
        tft.drawCircle(cx, cy, 91, GC9A01A_CYAN);
        tft.drawCircle(cx, cy, 92, blendColor565(GC9A01A_BLACK, GC9A01A_CYAN, 0.4f));
    }
}

ClockSkin display_select_skin(ClockSkin current) {
    ClockSkin selected = current;
    drawSkinPreview(selected, true);

    unsigned long lastTiltCheck = 0;
    bool          lastTiltLeft  = false;
    bool          lastTiltRight = false;

    // Кнопка: ждём отпускания если уже зажата
    while (digitalRead(TOUCH_PIN) == HIGH) delay(10);
    unsigned long btnPressStart = 0;
    bool          btnWasHigh    = false;

    Serial.println("=== SELECT SKIN ===");

    while (true) {

        // ── Наклон: листаем скины ──────────────────────────────
        if (mpu_ok && millis() - lastTiltCheck > 200) {
            float gx, gy, gz;
            mpu_read_accel(gx, gy, gz);

            bool tiltRight = (gy >  TILT_THRESHOLD);
            bool tiltLeft  = (gy < -TILT_THRESHOLD);

            // Срабатываем только на новый наклон (не держание)
            if (tiltRight && !lastTiltRight) {
                selected = (ClockSkin)(((int)selected + 1) % SKIN_COUNT);
                drawSkinPreview(selected, true);
                Serial.printf("Skin → %d\n", (int)selected);
            } else if (tiltLeft && !lastTiltLeft) {
                selected = (ClockSkin)(((int)selected - 1 + SKIN_COUNT) % SKIN_COUNT);
                drawSkinPreview(selected, true);
                Serial.printf("Skin ← %d\n", (int)selected);
            }

            lastTiltRight = tiltRight;
            lastTiltLeft  = tiltLeft;
            lastTiltCheck = millis();
        }

        // ── Кнопка: короткое нажатие = подтвердить ────────────
        int btn = digitalRead(TOUCH_PIN);

        if (btn == HIGH && !btnWasHigh) {
            btnPressStart = millis();
            btnWasHigh    = true;
        }

        if (btn == LOW && btnWasHigh) {
            unsigned long dur = millis() - btnPressStart;
            if (dur > 30 && dur < LONG_PRESS_MS) {
                // Короткое нажатие — выбрать
                Serial.printf("Skin SELECTED: %d\n", (int)selected);
                break;
            }
            btnWasHigh = false;
        }

        delay(20);
    }

    return selected;
}


//
// МАКЕТ (круглый экран 240x240):
//   Иконка 64x64   → центр X=120, верх Y=28  (x=88, y=28)
//   Температура     → слева,  y=118
//   Влажность       → справа, y=118
//   Описание погоды → внизу,  y=170
//
// Для изменения позиций — меняй константы ICON_X, ICON_Y и т.д.
// ============================================================

void display_draw_weather() {
    tft.fillScreen(GC9A01A_BLACK);

    // ── Позиции элементов ──────────────────────────────────
    const int ICON_X    = 88;   // Левый край иконки (120 - 64/2)
    const int ICON_Y    = 40;   // Верхний край иконки
    const int ICON_SIZE = 64;   // Размер иконки

    const int TEMP_X    = 40;   // Начало блока температуры
    const int HUM_X     = 130;  // Начало блока влажности
    const int DATA_Y    = 110;  // Y для цифр температуры/влажности
    const int LABEL_Y   = 100;  // Y для подписей "TEMP" / "HUM"
    const int DESC_Y    = 158;  // Y для текста описания погоды
    const int CITY_Y    = 176;  // Y для названия города

    // ── Выбор иконки по коду weather_id ───────────────────
    const uint16_t* icon = icon_sunny;  // по умолчанию

    if (weather_id == 800) {
        icon = icon_sunny;
    } else if (weather_id == 801) {
        icon = icon_few_clouds;
    } else if (weather_id == 802) {
        icon = icon_scattered;
    } else if (weather_id == 803) {
        icon = icon_broken;
    } else if (weather_id == 804) {
        icon = icon_overcast;
    } else if (weather_id >= 200 && weather_id < 300) {
        icon = icon_thunder;
    } else if (weather_id >= 600 && weather_id < 700) {
        icon = icon_snow;   // Снег (600-699) — проверяем ДО дождя
    } else if (weather_id >= 300 && weather_id < 600) {
        icon = icon_rain;   // Дождь и морось (300-599)
    } else {
        // Туман, дымка, прочее → показываем overcast
        icon = icon_overcast;
    }

    // ── Рисуем иконку ──────────────────────────────────────
    drawIcon(ICON_X, ICON_Y, icon, ICON_SIZE, ICON_SIZE);

    // ── Разделительная линия ───────────────────────────────
    tft.drawFastHLine(20, 97, 200, 0x2945);  // тёмно-серая линия

    // ── Подписи (маленькие, серые) ─────────────────────────
    tft.setTextSize(1);
    tft.setTextColor(0x7BEF);  // серый

    tft.setCursor(TEMP_X + 6, LABEL_Y);
    tft.print("TEMP");

    tft.setCursor(HUM_X + 8, LABEL_Y);
    tft.print("HUM");

    // ── Температура ────────────────────────────────────────
    char tempBuf[10];
    // Форматируем температуру: целое число + "°C"
    int tempInt = (int)round(temperature);
    sprintf(tempBuf, "%d", tempInt);

    tft.setTextSize(3);
    tft.setTextColor(GC9A01A_MAGENTA);
    tft.setCursor(TEMP_X, DATA_Y);
    tft.print(tempBuf);
    tft.setTextSize(2);
    tft.print("\xF7""C");  // символ градуса через hex (°C)

    // ── Влажность ──────────────────────────────────────────
    tft.setTextSize(3);
    tft.setTextColor(GC9A01A_CYAN);
    tft.setCursor(HUM_X, DATA_Y);
    tft.print(humidity);
    tft.setTextSize(2);
    tft.print("%");

    // ── Описание погоды (строка от API, например "clear sky") ──
    // Обрезаем до 18 символов, чтобы влезло на экран
    String shortDesc = description;
    shortDesc.toUpperCase();
    if (shortDesc.length() > 16) shortDesc = shortDesc.substring(0, 16);

    tft.setTextSize(1);
    tft.setTextColor(GC9A01A_WHITE);
    // Центрируем текст: 1 символ = 6 px при size=1
    int descWidth = shortDesc.length() * 6;
    int descX = 120 - descWidth / 2;
    tft.setCursor(descX, DESC_Y);
    tft.print(shortDesc);

    // ── Название города ────────────────────────────────────
    tft.setTextSize(1);
    tft.setTextColor(0x7BEF);  // серый
    String cityStr = String(CITY);
    cityStr.toUpperCase();
    int cityWidth = cityStr.length() * 6;
    int cityX = 120 - cityWidth / 2;
    tft.setCursor(cityX, CITY_Y);
    tft.print(cityStr);
}

// ============================================================
// ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ДЛЯ АНИМАЦИИ 8th CUBE
// ============================================================

// Смешивает два цвета RGB565 в пропорции t (0.0 = цвет a, 1.0 = цвет b)
static uint16_t blendColor565(uint16_t a, uint16_t b, float t) {
    // Распаковываем компоненты RGB565
    uint8_t r_a = (a >> 11) & 0x1F;
    uint8_t g_a = (a >> 5)  & 0x3F;
    uint8_t b_a =  a        & 0x1F;

    uint8_t r_b = (b >> 11) & 0x1F;
    uint8_t g_b = (b >> 5)  & 0x3F;
    uint8_t b_b =  b        & 0x1F;

    uint8_t r = r_a + (uint8_t)((r_b - (int)r_a) * t);
    uint8_t g = g_a + (uint8_t)((g_b - (int)g_a) * t);
    uint8_t bv = b_a + (uint8_t)((b_b - (int)b_a) * t);

    return (r << 11) | (g << 5) | bv;
}

// Рисует большую цифру "8" в стиле логотипа куба
// cx, cy — центр; size — размер шрифта
static void drawBig8(int cx, int cy, uint16_t color) {
    // Белый квадрат (как логотип)
    int sq = 80;
    tft.fillRect(cx - sq/2, cy - sq/2, sq, sq, color);
    // Цифра "8" чёрным внутри
    tft.setTextColor(GC9A01A_BLACK);
    tft.setTextSize(8);
    tft.setCursor(cx - 24, cy - 36);
    tft.print("8");
    // Надпись "th" маленьким
    tft.setTextSize(2);
    tft.setCursor(cx + 14, cy - 32);  // сдвинуто ближе к "8": было +26, стало +14
    tft.print("th");
}

// Рисует пирамидку (треугольник) с цветом — основа для ответа
// cx — центр по X, y_top — верхняя точка, height — высота
static void drawPyramid(int cx, int y_top, int height, uint16_t color) {
    int half_base = height * 3 / 4;
    // Заливка треугольника через горизонтальные линии
    for (int row = 0; row < height; row++) {
        float progress = (float)row / height;
        int half_w = (int)(half_base * progress);
        tft.drawFastHLine(cx - half_w, y_top + row, half_w * 2, color);
    }
}

// Рисует кольцо-ореол вокруг точки (эффект сияния)
static void drawGlow(int cx, int cy, int radius, uint16_t color) {
    // Рисуем несколько концентрических окружностей с убывающей яркостью
    for (int i = 0; i < 4; i++) {
        uint16_t c = blendColor565(GC9A01A_BLACK, color, 0.25f * (4 - i) / 4.0f);
        tft.drawCircle(cx, cy, radius + i, c);
    }
}

// Выводит текст по горизонтальному центру экрана
// yPos — координата Y; size — размер шрифта; color — цвет
static void drawCenteredText(const char* text, int yPos, int size, uint16_t color) {
    int textWidth = strlen(text) * 6 * size;
    int xPos = 120 - textWidth / 2;
    if (xPos < 5) xPos = 5;
    tft.setTextColor(color);
    tft.setTextSize(size);
    tft.setCursor(xPos, yPos);
    tft.print(text);
}

// ============================================================
// ЭКРАН ОЖИДАНИЯ 8th CUBE
// Показывает логотип "8" и "SHAKE ME!" с пульсацией
// ============================================================

void display_draw_magic_ball() {
    tft.fillScreen(GC9A01A_BLACK);

    // ── Большая "8" в белом квадрате (центр экрана чуть выше) ──
    drawBig8(120, 100, GC9A01A_WHITE);

    // ── Надпись "SHAKE ME!" снизу ──────────────────────────────
    // Рисуем с эффектом "свечения" — чуть более широкая тёмная тень
    uint16_t glowColor = 0x02EF;  // тёмно-синий
    drawCenteredText("SHAKE ME!", 165, 2, glowColor);
    drawCenteredText("SHAKE ME!", 166, 2, glowColor);
    drawCenteredText("SHAKE ME!", 165, 2, GC9A01A_CYAN);

}

// ============================================================
// АНИМАЦИЯ ПРЕДСКАЗАНИЯ — fade in / пауза / fade out
//
// Этапы:
//   1. Волна расходится от центра (реакция на тряску)
//   2. Fade in — текст появляется из темноты (чёрный → cyan)
//   3. Пауза 3 секунды — текст горит ярко
//   4. Fade out — текст уходит в темноту (cyan → чёрный)
//   5. Возврат на экран ожидания
// ============================================================

void display_show_prediction() {
    // ── Выбираем случайный ответ ───────────────────────────────
    int idx = random(answersCount);
    const char* prediction = answers[idx];

    // ── Размер текста по длине слова ───────────────────────────
    int textSize;
    int textLen = strlen(prediction);
    if      (textLen <= 3)  textSize = 5;  // "Yes", "No"
    else if (textLen <= 5)  textSize = 4;  // "Maybe", "Never"
    else if (textLen <= 8)  textSize = 3;  // "Likely", "Unlikely"
    else                    textSize = 2;  // "Ask later", "Not now"

    // Высота одного символа в пикселях при данном размере
    int charH = 8 * textSize;
    int charW = 6 * textSize;

    // Центрируем текст по горизонтали и вертикали
    int textWidth  = textLen * charW;
    int textX = 120 - textWidth / 2;
    int textY = 120 - charH / 2;   // вертикальный центр экрана
    if (textX < 4) textX = 4;

    // ── Финальный цвет текста из палитры устройства ────────────
    // Cyan — основной акцентный цвет палитры ГАЛАКТИКА
    uint16_t colorFinal = GC9A01A_CYAN;

    // ═══════════════════════════════════════════════════════════
    // ЭТАП 1: Волны от центра (реакция на тряску)
    // ═══════════════════════════════════════════════════════════
    tft.fillScreen(GC9A01A_BLACK);

    for (int wave = 0; wave < 3; wave++) {
        for (int r = 10; r <= 115; r += 3) {
            float alpha = 1.0f - (float)r / 115.0f;
            uint16_t waveColor = blendColor565(GC9A01A_BLACK, colorFinal, alpha * 0.6f);
            tft.drawCircle(120, 120, r, waveColor);
            if (r > 13) {
                tft.drawCircle(120, 120, r - 3, GC9A01A_BLACK);
            }
            delay(4);
        }
        for (int r = 10; r <= 115; r += 3) {
            tft.drawCircle(120, 120, r, GC9A01A_BLACK);
        }
        delay(60);
    }

    // Экран чистый после волн
    tft.fillScreen(GC9A01A_BLACK);

    // ═══════════════════════════════════════════════════════════
    // ЭТАП 2: Fade in — текст появляется из темноты
    // Цвет идёт от чёрного через тёмный синий к яркому cyan
    // Текст стоит на месте, меняется только цвет
    // ═══════════════════════════════════════════════════════════

    const int FADE_IN_STEPS = 28;  // количество кадров появления

    for (int step = 0; step <= FADE_IN_STEPS; step++) {
        float t = (float)step / FADE_IN_STEPS;

        // Easing: медленный старт, быстрый финиш (ease-in)
        float ease = t * t;

        // Цвет: чёрный → тёмный cyan (первые 40%) → яркий cyan (последние 60%)
        uint16_t cur_color;
        if (t < 0.4f) {
            // Из полной темноты в тёмный синий — почти незаметно
            cur_color = blendColor565(GC9A01A_BLACK, 0x034B, t / 0.4f);
        } else {
            // Из тёмного синего в яркий cyan — основная часть появления
            cur_color = blendColor565(0x034B, colorFinal, (t - 0.4f) / 0.6f);
        }

        // Стираем предыдущий кадр (перерисовываем тем же прямоугольником)
        if (step > 0) {
            tft.fillRect(textX - 2, textY - 2, textWidth + 4, charH + 4, GC9A01A_BLACK);
        }

        // Рисуем текст с текущим цветом
        tft.setTextColor(cur_color);
        tft.setTextSize(textSize);
        tft.setCursor(textX, textY);
        tft.print(prediction);

        delay(28);
    }

    // ── Финальный кадр — убеждаемся что цвет точно правильный ──
    tft.fillRect(textX - 2, textY - 2, textWidth + 4, charH + 4, GC9A01A_BLACK);
    tft.setTextColor(colorFinal);
    tft.setTextSize(textSize);
    tft.setCursor(textX, textY);
    tft.print(prediction);

    // ═══════════════════════════════════════════════════════════
    // ЭТАП 3: Пауза — текст горит
    // ═══════════════════════════════════════════════════════════
    delay(3000);

    // ═══════════════════════════════════════════════════════════
    // ЭТАП 4: Fade out — текст уходит в темноту
    // Зеркально fade in: cyan → тёмный синий → чёрный
    // ═══════════════════════════════════════════════════════════

    const int FADE_OUT_STEPS = 24;

    for (int step = 0; step <= FADE_OUT_STEPS; step++) {
        float t = (float)step / FADE_OUT_STEPS;

        // Easing: быстрый старт, медленный финиш (ease-out)
        float ease = 1.0f - (1.0f - t) * (1.0f - t);

        uint16_t cur_color;
        if (ease < 0.6f) {
            cur_color = blendColor565(colorFinal, 0x034B, ease / 0.6f);
        } else {
            cur_color = blendColor565(0x034B, GC9A01A_BLACK, (ease - 0.6f) / 0.4f);
        }

        tft.fillRect(textX - 2, textY - 2, textWidth + 4, charH + 4, GC9A01A_BLACK);
        tft.setTextColor(cur_color);
        tft.setTextSize(textSize);
        tft.setCursor(textX, textY);
        tft.print(prediction);

        delay(30);
    }

    // ═══════════════════════════════════════════════════════════
    // ЭТАП 5: Возврат на экран ожидания
    // ═══════════════════════════════════════════════════════════
    delay(200);
    display_clear();
}

// ============================================================
// ФУНКЦИЯ display_draw_arc_ring (используется в других местах)
// ============================================================

void display_draw_arc_ring(uint16_t color, float progress, int radius, int thickness) {
    drawArcSegment(color, 0.0f, progress, radius, thickness);
}