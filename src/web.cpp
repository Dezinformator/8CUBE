#include "web.h"
#include "config.h"
#include "display.h"
#include "weather.h"
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>

// ============================================================
// ФЛАГИ — пишутся обработчиками сервера, читаются в loop()
// ============================================================

volatile bool web_flag_mode_changed   = false;
volatile bool web_flag_skin_changed   = false;
volatile bool web_flag_settings_saved = false;
volatile int  web_new_mode            = 0;
volatile int  web_new_skin            = 0;

// ============================================================
// HTML — встроен в прошивку, PROGMEM не занимает RAM
// ============================================================

static const char PAGE_HTML[] PROGMEM = R"RAW(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>8th CUBE</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{background:#050a12;color:#c8d8e8;font-family:'Courier New',monospace;min-height:100vh;padding:20px}
h1{text-align:center;font-size:1.6em;letter-spacing:.2em;color:#00e5ff;text-shadow:0 0 12px #00e5ff88;margin-bottom:6px}
.sub{text-align:center;font-size:.75em;color:#334;margin-bottom:24px;letter-spacing:.1em}
.card{background:#0d1520;border:1px solid #1a2a3a;border-radius:10px;padding:18px;margin-bottom:14px}
.card h2{font-size:.75em;letter-spacing:.15em;color:#445;margin-bottom:14px;text-transform:uppercase}
.row{display:flex;gap:8px;flex-wrap:wrap}
button{flex:1;min-width:80px;padding:10px 6px;border:1px solid #1a3a5a;border-radius:6px;background:#0a1825;color:#7799bb;font-family:'Courier New',monospace;font-size:.82em;cursor:pointer;transition:all .15s;letter-spacing:.05em}
button:hover{background:#112233;border-color:#00e5ff33}
button.on{background:#001a2a;border-color:#00e5ff;color:#00e5ff;text-shadow:0 0 8px #00e5ff66;box-shadow:0 0 8px #00e5ff11}
button.on.g{border-color:#00ff88;color:#00ff88;text-shadow:0 0 8px #00ff8866}
button.on.m{border-color:#ff44cc;color:#ff44cc;text-shadow:0 0 8px #ff44cc66}
label{display:block;font-size:.72em;color:#445;margin-bottom:4px;letter-spacing:.08em;text-transform:uppercase}
input{width:100%;padding:9px 11px;background:#060e18;border:1px solid #1a2a3a;border-radius:6px;color:#aac8e0;font-family:'Courier New',monospace;font-size:.88em;margin-bottom:11px;outline:none;transition:border-color .15s}
input:focus{border-color:#00e5ff44}
.frow{display:flex;gap:10px}
.frow>div{flex:1}
.sbtn{width:100%;padding:11px;background:#001a2a;border:1px solid #00e5ff44;border-radius:6px;color:#00e5ff;font-family:'Courier New',monospace;font-size:.88em;letter-spacing:.1em;cursor:pointer;transition:all .15s;margin-top:2px}
.sbtn:hover{background:#002233;border-color:#00e5ff;box-shadow:0 0 10px #00e5ff22}
.sbar{background:#060e18;border:1px solid #1a2a3a;border-radius:8px;padding:11px 16px;margin-bottom:14px;display:flex;justify-content:space-between;flex-wrap:wrap;gap:8px;font-size:.76em}
.si{color:#334}.si span{color:#00e5ff}
.toast{position:fixed;bottom:22px;left:50%;transform:translateX(-50%);background:#001a2a;border:1px solid #00e5ff;color:#00e5ff;padding:9px 22px;border-radius:20px;font-size:.82em;letter-spacing:.1em;opacity:0;transition:opacity .3s;pointer-events:none;white-space:nowrap}
.toast.show{opacity:1}
.auth{max-width:300px;margin:70px auto 0}
.auth h1{margin-bottom:28px}
#err{color:#ff4466;font-size:.78em;margin-top:7px;min-height:16px}
</style>
</head>
<body>

<div id="A" class="auth">
  <h1>8th CUBE</h1>
  <div class="card">
    <h2>Access</h2>
    <label>Password</label>
    <input type="password" id="pw" placeholder="password" onkeydown="if(event.key==='Enter')login()">
    <button class="sbtn" onclick="login()">CONNECT</button>
    <div id="err"></div>
  </div>
</div>

<div id="M" style="display:none;max-width:460px;margin:0 auto">
  <h1>8th CUBE</h1>
  <div class="sub" id="ip">—</div>

  <div class="sbar">
    <div class="si">MODE <span id="sM">—</span></div>
    <div class="si">SKIN <span id="sS">—</span></div>
    <div class="si">WiFi <span id="sW">—</span></div>
    <div class="si">NTP <span id="sN">—</span></div>
  </div>

  <div class="card">
    <h2>Mode</h2>
    <div class="row">
      <button id="b0" onclick="setMode(0)">CLOCK</button>
      <button id="b1" onclick="setMode(1)">WEATHER</button>
      <button id="b2" onclick="setMode(2)" class="m">MAGIC BALL</button>
    </div>
  </div>

  <div class="card">
    <h2>Clock Skin</h2>
    <div class="row">
      <button id="s0" onclick="setSkin(0)">ARCS</button>
      <button id="s1" onclick="setSkin(1)" class="g">PLANETS</button>
    </div>
  </div>

  <div class="card">
    <h2>Weather Settings</h2>
    <label>City name (display)</label>
    <input id="cCity" placeholder="Smolensk">
    <div class="frow">
      <div><label>Latitude</label><input id="cLat" placeholder="54.7818"></div>
      <div><label>Longitude</label><input id="cLon" placeholder="32.0401"></div>
    </div>
    <label>Timezone offset (hours)</label>
    <input id="cTz" placeholder="3">
    <button class="sbtn" onclick="saveSettings()">SAVE &amp; APPLY</button>
  </div>

  <div class="card">
    <h2>Wi-Fi Networks</h2>
    <div id="wList" style="margin-bottom:12px"></div>
    <label>Add network</label>
    <input id="wS" placeholder="Network name (SSID)">
    <input type="password" id="wP" placeholder="Password (leave empty if open)">
    <button class="sbtn" onclick="addWifi()">ADD NETWORK</button>
  </div>
</div>

<div class="toast" id="t"></div>

<script>
let ok=false;
function login(){
  fetch('/api/auth',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({password:document.getElementById('pw').value})})
  .then(r=>r.json()).then(d=>{
    if(d.ok){ok=true;document.getElementById('A').style.display='none';document.getElementById('M').style.display='block';load();}
    else document.getElementById('err').textContent='wrong password';
  }).catch(()=>document.getElementById('err').textContent='connection error');
}
function load(){
  fetch('/api/status').then(r=>r.json()).then(d=>{
    document.getElementById('sM').textContent=['CLOCK','WEATHER','MAGIC BALL'][d.mode]||'?';
    document.getElementById('sS').textContent=['ARCS','PLANETS'][d.skin]||'?';
    document.getElementById('sW').textContent=d.wifi?'OK':'NO';
    document.getElementById('sN').textContent=d.ntp?'SYNC':'NO';
    document.getElementById('ip').textContent=d.ip;
    document.getElementById('cCity').value=d.city||'';
    document.getElementById('cLat').value=d.lat||'';
    document.getElementById('cLon').value=d.lon||'';
    document.getElementById('cTz').value=d.tz||3;
    mUI(d.mode);sUI(d.skin);loadWifi();
  });
}
function mUI(m){[0,1,2].forEach(i=>{let b=document.getElementById('b'+i);b.classList.toggle('on',i===m);});}
function sUI(s){[0,1].forEach(i=>{document.getElementById('s'+i).classList.toggle('on',i===s);});}
function setMode(m){
  fetch('/api/mode',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({mode:m})})
  .then(r=>r.json()).then(d=>{if(d.ok){mUI(m);toast('Mode changed');}});
}
function setSkin(s){
  fetch('/api/skin',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({skin:s})})
  .then(r=>r.json()).then(d=>{if(d.ok){sUI(s);toast('Skin changed');}});
}
function saveSettings(){
  fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({
    city:document.getElementById('cCity').value,
    lat:document.getElementById('cLat').value,
    lon:document.getElementById('cLon').value,
    tz:parseInt(document.getElementById('cTz').value)||3
  })}).then(r=>r.json()).then(d=>{if(d.ok)toast('Saved');});
}
function loadWifi(){
  fetch('/api/wifi/list').then(r=>r.json()).then(d=>{
    let html='';
    if(d.networks.length===0){
      html='<div style="color:#334;font-size:.78em;margin-bottom:8px">No saved networks</div>';
    } else {
      d.networks.forEach((n,i)=>{
        html+=`<div style="display:flex;justify-content:space-between;align-items:center;padding:7px 0;border-bottom:1px solid #1a2a3a">
          <span style="color:#aac8e0;font-size:.88em">${n}</span>
          <button onclick="delWifi(${i})" style="flex:0;min-width:auto;padding:4px 10px;font-size:.75em;border-color:#ff4466;color:#ff4466">DEL</button>
        </div>`;
      });
    }
    document.getElementById('wList').innerHTML=html;
  });
}
function addWifi(){
  let s=document.getElementById('wS').value.trim();
  if(!s){toast('Enter SSID');return;}
  fetch('/api/wifi/add',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({ssid:s,pwd:document.getElementById('wP').value})})
  .then(r=>r.json()).then(d=>{
    if(d.ok){
      document.getElementById('wS').value='';
      document.getElementById('wP').value='';
      toast('Network saved');
      loadWifi();
    } else {
      toast(d.err||'Error');
    }
  });
}
function delWifi(i){
  fetch('/api/wifi/delete',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({index:i})})
  .then(r=>r.json()).then(d=>{if(d.ok){toast('Deleted');loadWifi();}});
}
function toast(m){let t=document.getElementById('t');t.textContent=m;t.classList.add('show');setTimeout(()=>t.classList.remove('show'),2500);}
setInterval(()=>{if(ok)load();},5000);
</script>
</body>
</html>)RAW";

// ============================================================
// СОСТОЯНИЕ
// ============================================================

static AsyncWebServer server(80);
static Preferences    prefs;
static String webPassword  = "cube";
static String cfg_city     = "Smolensk";
static String cfg_lat      = "54.7818";
static String cfg_lon      = "32.0401";
static int    cfg_tz       = 3;

void web_load_prefs() {
    prefs.begin("8cube", true);
    webPassword = prefs.getString("webpwd", "cube");
    cfg_city    = prefs.getString("city",   "Smolensk");
    cfg_lat     = prefs.getString("lat",    "54.7818");
    cfg_lon     = prefs.getString("lon",    "32.0401");
    cfg_tz      = prefs.getInt   ("tz",     3);
    prefs.end();
}

bool web_is_ap_mode() {
    prefs.begin("8cube", true);
    bool ap = prefs.getBool("ap_mode", false);
    prefs.end();
    return ap;
}

String web_get_ip()   { return WiFi.localIP().toString(); }
String web_get_city() { return cfg_city; }
String web_get_lat()  { return cfg_lat;  }
String web_get_lon()  { return cfg_lon;  }
int    web_get_tz()   { return cfg_tz;   }

// ── Режим точки доступа ───────────────────────────────────────
// Вызывается по тройному длинному нажатию.
// ESP32 переходит в режим AP+STA — старое соединение падает,
// поднимается своя сеть "8th-CUBE". Сервер продолжает работать.
void web_start_ap() {
    // Сохраняем флаг в Preferences — после перезагрузки
    // setup() увидит его и стартует сразу в режиме AP
    // без попытки подключиться к WiFi
    prefs.begin("8cube", false);
    prefs.putBool("ap_mode", true);
    prefs.end();

    Serial.println("AP mode flag saved — restarting...");
    delay(200);
    ESP.restart();
}

// Вызывается из setup() если ap_mode флаг был установлен.
// Стартует чистый AP без конфликтов с STA стеком.
// Forward declarations
static void web_register_routes();
static void web_init_server_internal();

void web_run_ap_mode() {
    const char* AP_SSID = "8th-CUBE";

    // Сбрасываем флаг — при следующей перезагрузке нормальный режим
    prefs.begin("8cube", false);
    prefs.putBool("ap_mode", false);
    prefs.end();

    // Полный сброс WiFi стека перед стартом AP
    WiFi.persistent(false);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(1000);  // даём стеку полностью упасть

    // Конфигурируем AP адреса ДО включения режима
    IPAddress local_ip(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);

    WiFi.mode(WIFI_AP);
    delay(200);

    WiFi.softAPConfig(local_ip, gateway, subnet);
    delay(100);

    // Открытая сеть, канал 6, не скрытая, макс 4 клиента
    bool ok = WiFi.softAP(AP_SSID, nullptr, 6, false, 4);
    delay(2000);  // критично: ESP32 нужно время чтобы DHCP сервер стартовал

    IPAddress ip = WiFi.softAPIP();
    Serial.printf("AP %s — SSID: %s  IP: %s  clients: %d\n",
        ok ? "OK" : "FAIL", AP_SSID, ip.toString().c_str(),
        WiFi.softAPgetStationNum());

    // Запускаем HTTP сервер
    web_init_server_internal();
}

// ============================================================
// ИНИЦИАЛИЗАЦИЯ СЕРВЕРА
// ============================================================

// Регистрирует все маршруты — вызывается и из web_init и из web_run_ap_mode
static void web_register_routes() {
    web_load_prefs();

    // Главная страница
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send_P(200, "text/html", PAGE_HTML);
    });

    // POST /api/auth
    server.on("/api/auth", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
            JsonDocument doc;
            deserializeJson(doc, data, len);
            bool ok = (String(doc["password"] | "") == webPassword);
            req->send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
        }
    );

    // GET /api/status
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
        extern int  currentMode;
        extern bool wifi_ok;
        extern bool time_synced;
        JsonDocument doc;
        doc["mode"] = currentMode;
        doc["skin"] = (int)display_get_skin();
        doc["wifi"] = wifi_ok;
        doc["ntp"]  = time_synced;
        doc["ip"]   = (WiFi.getMode() == WIFI_AP)
                      ? WiFi.softAPIP().toString()
                      : WiFi.localIP().toString();
        doc["city"] = cfg_city;
        doc["lat"]  = cfg_lat;
        doc["lon"]  = cfg_lon;
        doc["tz"]   = cfg_tz;
        String out; serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    // POST /api/mode
    server.on("/api/mode", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
            JsonDocument doc;
            deserializeJson(doc, data, len);
            int m = doc["mode"] | 0;
            if (m >= 0 && m <= 2) { web_new_mode = m; web_flag_mode_changed = true; }
            req->send(200, "application/json", "{\"ok\":true}");
        }
    );

    // POST /api/skin
    server.on("/api/skin", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
            JsonDocument doc;
            deserializeJson(doc, data, len);
            int s = doc["skin"] | 0;
            if (s >= 0 && s < SKIN_COUNT) { web_new_skin = s; web_flag_skin_changed = true; }
            req->send(200, "application/json", "{\"ok\":true}");
        }
    );

    // POST /api/settings
    server.on("/api/settings", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
            JsonDocument doc;
            deserializeJson(doc, data, len);
            cfg_city = doc["city"] | cfg_city.c_str();
            cfg_lat  = doc["lat"]  | cfg_lat.c_str();
            cfg_lon  = doc["lon"]  | cfg_lon.c_str();
            cfg_tz   = doc["tz"]   | cfg_tz;
            prefs.begin("8cube", false);
            prefs.putString("city", cfg_city);
            prefs.putString("lat",  cfg_lat);
            prefs.putString("lon",  cfg_lon);
            prefs.putInt   ("tz",   cfg_tz);
            prefs.end();
            web_flag_settings_saved = true;
            req->send(200, "application/json", "{\"ok\":true}");
        }
    );

    // GET /api/wifi/list
    server.on("/api/wifi/list", HTTP_GET, [](AsyncWebServerRequest* req) {
        prefs.begin("8cube", true);
        int count = prefs.getInt("wifi_count", 0);
        JsonDocument doc;
        JsonArray arr = doc["networks"].to<JsonArray>();
        for (int i = 0; i < count; i++) {
            String ssid = prefs.getString(("wssid" + String(i)).c_str(), "");
            if (ssid.length() > 0) arr.add(ssid);
        }
        prefs.end();
        String out; serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    // POST /api/wifi/add
    server.on("/api/wifi/add", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
            JsonDocument doc;
            deserializeJson(doc, data, len);
            String ssid = doc["ssid"] | "";
            String pwd  = doc["pwd"]  | "";
            if (ssid.length() == 0) {
                req->send(200, "application/json", "{\"ok\":false,\"err\":\"empty SSID\"}");
                return;
            }
            prefs.begin("8cube", false);
            int count = prefs.getInt("wifi_count", 0);
            if (count >= 5) {
                prefs.end();
                req->send(200, "application/json", "{\"ok\":false,\"err\":\"max 5 networks\"}");
                return;
            }
            prefs.putString(("wssid" + String(count)).c_str(), ssid);
            prefs.putString(("wpwd"  + String(count)).c_str(), pwd);
            prefs.putInt("wifi_count", count + 1);
            prefs.end();
            req->send(200, "application/json", "{\"ok\":true}");
        }
    );

    // POST /api/wifi/delete
    server.on("/api/wifi/delete", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
            JsonDocument doc;
            deserializeJson(doc, data, len);
            int idx = doc["index"] | -1;
            prefs.begin("8cube", false);
            int count = prefs.getInt("wifi_count", 0);
            if (idx < 0 || idx >= count) {
                prefs.end();
                req->send(200, "application/json", "{\"ok\":false}");
                return;
            }
            for (int i = idx; i < count - 1; i++) {
                String s = prefs.getString(("wssid" + String(i+1)).c_str(), "");
                String p2 = prefs.getString(("wpwd"  + String(i+1)).c_str(), "");
                prefs.putString(("wssid" + String(i)).c_str(), s);
                prefs.putString(("wpwd"  + String(i)).c_str(), p2);
            }
            prefs.remove(("wssid" + String(count-1)).c_str());
            prefs.remove(("wpwd"  + String(count-1)).c_str());
            prefs.putInt("wifi_count", count - 1);
            prefs.end();
            req->send(200, "application/json", "{\"ok\":true}");
        }
    );

    server.onNotFound([](AsyncWebServerRequest* req) {
        req->send(404, "text/plain", "not found");
    });
}
// Внутренняя функция запуска сервера
static void web_init_server_internal() {
    web_register_routes();
    server.begin();
}

void web_init() {
    web_load_prefs();
    web_init_server_internal();
    Serial.printf("Web UI: http://%s  pwd: %s\n",
                  WiFi.localIP().toString().c_str(), webPassword.c_str());
}