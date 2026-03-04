#pragma once
#include <Arduino.h>

void   web_init();
void   web_start_ap();      // Сохранить флаг и перезагрузиться в AP режим
void   web_run_ap_mode();   // Запустить чистый AP режим (вызывается из setup при старте)
bool   web_is_ap_mode();    // Проверить флаг AP в Preferences
void   web_load_prefs();
String web_get_ip();
String web_get_city();
String web_get_lat();
String web_get_lon();
int    web_get_tz();

// Флаги команд от веб-интерфейса
extern volatile bool web_flag_mode_changed;
extern volatile bool web_flag_skin_changed;
extern volatile bool web_flag_settings_saved;
extern volatile int  web_new_mode;
extern volatile int  web_new_skin;
