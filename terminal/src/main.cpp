/**
 * ============================================================================
 *  SmartQueue Terminal  v3.1
 * ============================================================================
 *  Принтер: bitbank2/Thermal_Printer (BLE)
 *
 *  API библиотеки (реальный, из wiki):
 *    tpScan(name, seconds)   — BLE-сканирование по имени
 *    tpConnect()             — подключиться к найденному
 *    tpConnect(mac_cstr)     — подключиться по MAC (std::string внутри!)
 *    tpDisconnect()          — отключиться
 *    tpIsConnected()         — статус
 *    tpGetWidth()            — ширина в пикселях (384 или 576)
 *    tpSetBackBuffer(buf, w, h) — задать буфер для графики
 *    tpDrawText(x,y,str,font,inv) — нарисовать текст в буфер
 *    tpPrintBuffer()         — отправить буфер на принтер
 *    tpFill(byte)            — залить буфер
 *    tpPrint(str)            — прямая ASCII-печать (без буфера)
 *    tpPrintLine(str)        — то же + перевод строки
 *    tpFeed(lines)           — прокрутка бумаги
 *    tpAlign(align)          — выравнивание (ALIGN_LEFT/CENTER/RIGHT)
 *    tpQRCode(str)           — QR-код
 *    tpSetWriteMode(mode)    — MODE_WITH_RESPONSE / MODE_WITHOUT_RESPONSE
 *    tpWriteRawData(buf,len) — отправка сырых байт
 *
 *  НЕТ: tpGetType(), tpSetTextWrap(), ucPrinterType не экспортируется.
 *  Кириллица: встроенные шрифты ASCII-only. Используем транслитерацию.
 *  Кастомный GFXfont с кириллицей: подключается через tpPrintCustomText().
 *
 *  Платы:
 *    esp32_wroom       — Bluedroid BLE
 *    esp32c3_supermini — NimBLE (флаг NIMBLE_SUPPORT)
 * ============================================================================
 */

#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WebServer.h>
#include <esp_wpa2.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <mbedtls/md.h>

#include <GyverDBFile.h>
#include <SettingsESP.h>

// bitbank2/Thermal_Printer
// NIMBLE_SUPPORT передаётся через build_flags для ESP32-C3
#include <Thermal_Printer.h>

// =============================================================================
//  Константы
// =============================================================================
#define FW_VERSION           "3.1.0"
#define DEFAULT_PRINTER_NAME "GT01"   // BLE-имя по умолчанию
#define POLL_INTERVAL_MS     5000UL
#define PRINT_WIDTH          384       // px; MTP-2=384, MTP-3=576

// Высота буфера в строках пикселей (под одну «страницу» печати)
// FONT_LARGE = 16x24 px, FONT_SMALL = 8x8 px
#define BUF_LINES   32

// =============================================================================
//  Ключи GyverDB
// =============================================================================
#define K_WIFI_MODE      "wifi_mode"_h
#define K_SSID           "ssid"_h
#define K_PSK            "psk"_h
#define K_EAP_METHOD     "eap_method"_h
#define K_EAP_IDENTITY   "eap_id"_h
#define K_EAP_USER       "eap_user"_h
#define K_EAP_PASS       "eap_pass"_h
#define K_EAP_CA_CERT    "eap_ca"_h
#define K_API_URL        "api_url"_h
#define K_API_QUEUE      "api_queue"_h
#define K_API_TOKEN      "api_token"_h
#define K_API_SECRET     "api_secret"_h
#define K_BT_NAME        "bt_name"_h
#define K_BT_MAC         "bt_mac"_h
#define K_WEB_PASS       "web_pass"_h
#define K_CONFIGURED     "configured"_h

// =============================================================================
//  Глобальные объекты
// =============================================================================
GyverDBFile  db(&LittleFS, "/sq_config.db");
SettingsESP  sett("SmartQueue Terminal", &db);

bool  wifiConnected    = false;
bool  printerConnected = false;
unsigned long lastPoll = 0;

// Растровый буфер: ширина / 8 байт на строку × BUF_LINES строк
static uint8_t gfxBuf[PRINT_WIDTH / 8 * BUF_LINES];

// =============================================================================
//  Транслитерация кириллицы → ASCII
//  Thermal_Printer поддерживает только коды 32-126.
//  Входная строка — UTF-8 (двухбайтные символы U+0400–U+04FF).
// =============================================================================
String transliterate(const String& src) {
    // Таблица: индекс = код символа - 0x410 (А=0, Б=1, ... я=63)
    static const char* const tbl[] = {
        "A","B","V","G","D","E","Zh","Z","I","J","K","L","M",
        "N","O","P","R","S","T","U","F","Kh","Ts","Ch","Sh","Shch",
        "","Y","","E","Yu","Ya",
        "a","b","v","g","d","e","zh","z","i","j","k","l","m",
        "n","o","p","r","s","t","u","f","kh","ts","ch","sh","shch",
        "","y","","e","yu","ya"
    };
    String out;
    out.reserve(src.length() * 2);
    for (size_t i = 0; i < src.length(); ) {
        uint8_t c = (uint8_t)src[i];
        if (c == 0xD0 && i + 1 < src.length()) {
            // U+0400–U+04FF, первый байт 0xD0 или 0xD1
            uint8_t c2 = (uint8_t)src[i + 1];
            int idx = -1;
            if (c2 >= 0x90 && c2 <= 0xBF) idx = c2 - 0x90;       // А(0x90)..я(0xBF)
            else if (c2 == 0x81)           { out += "Yo"; i += 2; continue; }  // Ё
            if (idx >= 0 && idx < 64)      { out += tbl[idx]; i += 2; continue; }
            i += 2; continue;
        } else if (c == 0xD1 && i + 1 < src.length()) {
            uint8_t c2 = (uint8_t)src[i + 1];
            int idx = -1;
            if (c2 >= 0x80 && c2 <= 0x8F) idx = c2 - 0x80 + 48;  // р(0)..я(15) → +48
            else if (c2 == 0x91)           { out += "yo"; i += 2; continue; }  // ё
            if (idx >= 0 && idx < 64)      { out += tbl[idx]; i += 2; continue; }
            i += 2; continue;
        } else {
            out += (char)c;
            i++;
        }
    }
    return out;
}

// =============================================================================
//  Вспомогательные функции печати
// =============================================================================

// Напечатать одну строку текста через буфер (с автоматическим сбросом)
// font: FONT_SMALL (8px высота) или FONT_LARGE (24px высота)
static void printLine(const char* text, int font, int align = ALIGN_CENTER) {
    int lineH = (font == FONT_LARGE) ? 24 : 8;
    if (lineH > BUF_LINES) lineH = BUF_LINES;

    // Вычислить x для выравнивания
    int printW = tpGetWidth();
    if (printW == 0) printW = PRINT_WIDTH;
    int charW  = (font == FONT_LARGE) ? 16 : 8;
    int textW  = strlen(text) * charW;
    int x = 0;
    if (align == ALIGN_CENTER) x = max(0, (printW - textW) / 2);
    else if (align == ALIGN_RIGHT) x = max(0, printW - textW);

    tpSetBackBuffer(gfxBuf, printW, lineH);
    tpFill(0x00);  // белый фон
    tpDrawText(x, 0, (char*)text, font, 0);
    tpPrintBuffer();
}

// Разделитель
static void printSeparator() {
    printLine("----------------------------------------", FONT_SMALL, ALIGN_LEFT);
}

// =============================================================================
//  Печать талона
// =============================================================================
void printTicket(const String& num, const String& queue, const String& eta) {
    if (!printerConnected) {
        Serial.println("[Printer] not connected");
        return;
    }

    // Всё через транслитерацию — FONT_SMALL/LARGE работают только с ASCII
    String numA   = transliterate(num);
    String queueA = transliterate(queue);
    String etaA   = transliterate(eta);

    // Шапка
    printLine("SMART QUEUE", FONT_LARGE, ALIGN_CENTER);
    printSeparator();

    // Название очереди
    printLine(queueA.c_str(), FONT_SMALL, ALIGN_CENTER);

    // Номер — крупно
    printLine(numA.c_str(), FONT_LARGE, ALIGN_CENTER);

    // Время ожидания
    if (etaA.length()) {
        String s = "Wait: " + etaA;
        printLine(s.c_str(), FONT_SMALL, ALIGN_LEFT);
    }

    printSeparator();

    // Прокрутка в конце
    tpFeed(40);

    Serial.printf("[Printer] Ticket %s printed\n", numA.c_str());
}

// =============================================================================
//  Подключение к принтеру
// =============================================================================
bool connectPrinter() {
    String btName = db[K_BT_NAME].toString();
    String btMac  = db[K_BT_MAC].toString();
    if (btName.isEmpty()) btName = DEFAULT_PRINTER_NAME;

    Serial.printf("[Printer] Scan for '%s'...\n", btName.c_str());

    // tpScan внутри сам вызывает BLEDevice::init()
    bool found = tpScan((char*)btName.c_str(), 5);

    if (!found) {
        // Попробуем по MAC (tpConnect принимает const char* → std::string внутри)
        if (btMac.length() == 17) {
            Serial.printf("[Printer] Scan failed, try MAC %s\n", btMac.c_str());
            // tpConnect(const char*) использует std::string — передаём c_str()
            if (!tpConnect(btMac.c_str())) {
                Serial.println("[Printer] MAC connect failed");
                printerConnected = false;
                return false;
            }
        } else {
            Serial.println("[Printer] Not found");
            printerConnected = false;
            return false;
        }
    } else {
        if (!tpConnect()) {
            Serial.println("[Printer] Connect after scan failed");
            printerConnected = false;
            return false;
        }
    }

    printerConnected = true;
    int w = tpGetWidth();
    Serial.printf("[Printer] Connected! Print width: %d px\n", w);

    // Приветствие
    printLine("SmartQueue v3", FONT_LARGE, ALIGN_CENTER);
    printLine("Ready", FONT_SMALL, ALIGN_CENTER);
    tpFeed(20);
    return true;
}

// =============================================================================
//  WiFi — Personal PSK
// =============================================================================
bool connectWifiPSK(const String& ssid, const String& psk) {
    Serial.printf("[WiFi] PSK: %s\n", ssid.c_str());
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), psk.c_str());
    for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) {
        delay(500); Serial.print('.');
    }
    Serial.println();
    return WiFi.status() == WL_CONNECTED;
}

// =============================================================================
//  WiFi — Enterprise 802.1X
// =============================================================================
bool connectWifiEnterprise(const String& ssid, int method,
                           const String& identity, const String& user,
                           const String& pass,     const String& caCert) {
    Serial.printf("[WiFi] Enterprise: %s (method=%d)\n", ssid.c_str(), method);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    esp_wifi_sta_wpa2_ent_disable();

    if (identity.length())
        esp_wifi_sta_wpa2_ent_set_identity(
            (uint8_t*)identity.c_str(), identity.length());

    if (method == 2) {
        if (caCert.length())
            esp_wifi_sta_wpa2_ent_set_ca_cert(
                (uint8_t*)caCert.c_str(), caCert.length() + 1);
    } else {
        if (user.length())
            esp_wifi_sta_wpa2_ent_set_username(
                (uint8_t*)user.c_str(), user.length());
        if (pass.length())
            esp_wifi_sta_wpa2_ent_set_password(
                (uint8_t*)pass.c_str(), pass.length());
        if (caCert.length())
            esp_wifi_sta_wpa2_ent_set_ca_cert(
                (uint8_t*)caCert.c_str(), caCert.length() + 1);
        if (method == 1)
            esp_wifi_sta_wpa2_ent_set_ttls_phase2_method(
                ESP_EAP_TTLS_PHASE2_PAP);
    }

    esp_wifi_sta_wpa2_ent_enable();
    WiFi.begin(ssid.c_str());
    for (int i = 0; i < 60 && WiFi.status() != WL_CONNECTED; i++) {
        delay(500); Serial.print('.');
    }
    Serial.println();
    return WiFi.status() == WL_CONNECTED;
}

void doConnect() {
    String ssid = db[K_SSID].toString();
    if (ssid.isEmpty()) return;
    bool ok = (db[K_WIFI_MODE].toInt() == 0)
        ? connectWifiPSK(ssid, db[K_PSK].toString())
        : connectWifiEnterprise(ssid,
              (int)db[K_EAP_METHOD].toInt(),
              db[K_EAP_IDENTITY].toString(),
              db[K_EAP_USER].toString(),
              db[K_EAP_PASS].toString(),
              db[K_EAP_CA_CERT].toString());
    wifiConnected = ok;
    if (ok)
        Serial.printf("[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());
    else
        Serial.println("[WiFi] Failed");
}

void startAP() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("SmartQueue-Setup", "12345678");
    Serial.printf("[WiFi] AP: http://%s\n",
                  WiFi.softAPIP().toString().c_str());
}

// =============================================================================
//  HMAC-SHA256 подпись запросов
// =============================================================================
String hmacSha256(const String& key, const String& data) {
    uint8_t out[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&ctx, (const uint8_t*)key.c_str(), key.length());
    mbedtls_md_hmac_update(&ctx, (const uint8_t*)data.c_str(), data.length());
    mbedtls_md_hmac_finish(&ctx, out);
    mbedtls_md_free(&ctx);
    String hex; hex.reserve(64);
    for (int i = 0; i < 32; i++) { char b[3]; snprintf(b,3,"%02x",out[i]); hex+=b; }
    return hex;
}

// =============================================================================
//  Опрос API
// =============================================================================
void pollServer() {
    String apiUrl   = db[K_API_URL].toString();
    String queueId  = db[K_API_QUEUE].toString();
    String token    = db[K_API_TOKEN].toString();
    String secret   = db[K_API_SECRET].toString();
    if (!wifiConnected || apiUrl.isEmpty() || queueId.isEmpty()) return;

    String url = apiUrl + "/api/queues/" + queueId + "/next_ticket/";
    WiFiClient wc; HTTPClient http;
    http.begin(wc, url);
    if (token.length())  http.addHeader("Authorization", "Token " + token);
    if (secret.length()) http.addHeader("X-Signature",
                             "HMAC-SHA256 " + hmacSha256(secret, "GET:"+url));
    http.addHeader("Content-Type", "application/json");

    int code = http.GET();
    if (code == 200) {
        String body = http.getString();
        Serial.printf("[API] 200: %s\n", body.c_str());
        auto jf = [](const String& j, const String& k) -> String {
            String sk="\""+k+"\":\""; int s=j.indexOf(sk);
            if(s<0) return ""; s+=sk.length();
            int e=j.indexOf('"',s); return (e<0)?"":j.substring(s,e);
        };
        String num   = jf(body,"number");
        String queue = jf(body,"queue");
        String eta   = jf(body,"eta");
        if (num.isEmpty()) num = body;
        if (queue.isEmpty()) queue = "Queue";
        printTicket(num, queue, eta);
    } else if (code != 204 && code != 404) {
        Serial.printf("[API] %d\n", code);
    }
    http.end();
}

// =============================================================================
//  Веб-интерфейс Settings
// =============================================================================
void buildUI(sets::Builder& b) {
    {
        sets::GuestAccess ga(b);
        sets::Group g(b, "Status");
        b.Label("lbl_ip"_h, "IP",
            wifiConnected ? WiFi.localIP().toString().c_str() : "No WiFi");
        b.Label("lbl_bt"_h, "Printer",
            printerConnected
                ? (String("OK, ") + String(tpGetWidth()) + "px").c_str()
                : "Disconnected");
        b.Label("lbl_fw"_h, "Firmware", FW_VERSION);
        b.Label("lbl_up"_h, "Uptime s", String(millis()/1000).c_str());
    }
    {
        sets::Menu m(b, "WiFi");
        b.Select(K_WIFI_MODE, "Mode", "Personal (PSK);Enterprise (EAP)");
        b.Input(K_SSID, "SSID");
        {
            sets::Group g(b, "WPA/WPA2-PSK");
            b.Pass(K_PSK, "Password");
        }
        {
            sets::Group g(b, "Enterprise 802.1X");
            b.Select(K_EAP_METHOD, "EAP Method",
                "PEAP-MSCHAPv2;EAP-TTLS-PAP;EAP-TLS");
            b.Input(K_EAP_IDENTITY, "Outer identity");
            b.Input(K_EAP_USER,     "Username");
            b.Pass (K_EAP_PASS,     "Password");
            b.Input(K_EAP_CA_CERT,  "CA cert (PEM, optional)");
        }
        {
            sets::Buttons btns(b);
            if (b.Button("btn_wifi"_h, "Save & Reconnect", sets::Colors::Mint)) {
                wifiConnected = false; WiFi.disconnect(true); delay(300);
                doConnect(); b.reload();
            }
        }
    }
    {
        sets::Menu m(b, "API Server");
        b.Input(K_API_URL,   "URL (http://host:port)");
        b.Input(K_API_QUEUE, "Queue UUID");
        {
            sets::Group g(b, "Auth");
            b.Pass(K_API_TOKEN,  "Bearer token");
            b.Pass(K_API_SECRET, "HMAC secret (X-Signature)");
        }
        {
            sets::Buttons btns(b);
            if (b.Button("btn_api"_h, "Test request")) pollServer();
        }
    }
    {
        sets::Menu m(b, "Printer BLE");
        b.Input(K_BT_NAME, "BLE name (for scan)");
        b.Input(K_BT_MAC,  "MAC (XX:XX:XX:XX:XX:XX, optional)");
        b.Label("lbl_names"_h, "Known names",
            "MTP-2, MPT-II, MPT-3, GT01, GB01, GB02, PeriPage+, T02, MX06");
        b.Label("lbl_pw"_h, "Print width px",
            printerConnected ? String(tpGetWidth()).c_str() : "—");
        b.Label("lbl_cyr"_h, "Cyrillic",
            "ASCII only — auto-transliterated");
        {
            sets::Buttons btns(b);
            if (b.Button("btn_scan"_h, "Scan & Connect", sets::Colors::Blue)) {
                printerConnected = false; tpDisconnect(); delay(200);
                connectPrinter(); b.reload();
            }
            if (b.Button("btn_test"_h, "Test print")) {
                printTicket("A042", "Test Queue", "~5 min");
            }
        }
    }
    {
        sets::Menu m(b, "Security");
        b.Pass(K_WEB_PASS, "Web UI password");
        {
            sets::Buttons btns(b);
            if (b.Button("btn_pass"_h, "Apply password")) {
                sett.setPass(db[K_WEB_PASS].toString().c_str());
            }
        }
    }
    {
        sets::Group g(b, "System");
        sets::Buttons btns(b);
        if (b.Button("btn_reboot"_h, "Reboot", sets::Colors::Red)) {
            delay(300); ESP.restart();
        }
    }
    // OTA — кнопка "Обновление" в правом меню браузера (встроено в Settings)
}

void updateUI(sets::Updater& upd) {
    upd.update("lbl_ip"_h,
        wifiConnected ? WiFi.localIP().toString().c_str() : "No WiFi");
    upd.update("lbl_bt"_h,
        printerConnected
            ? (String("OK, ") + String(tpGetWidth()) + "px").c_str()
            : "Disconnected");
    upd.update("lbl_up"_h, String(millis()/1000).c_str());
    upd.update("lbl_pw"_h,
        printerConnected ? String(tpGetWidth()).c_str() : "—");
}

// =============================================================================
//  SETUP
// =============================================================================
void setup() {
    Serial.begin(115200);
    delay(400);
    Serial.println("\n=== SmartQueue Terminal v3.1 ===");

    if (!LittleFS.begin(true)) Serial.println("[FS] fail");
    else                       Serial.println("[FS] OK");

    db.begin();

    db.init(K_WIFI_MODE,    (uint8_t)0);
    db.init(K_SSID,         "");
    db.init(K_PSK,          "");
    db.init(K_EAP_METHOD,   (uint8_t)0);
    db.init(K_EAP_IDENTITY, "anonymous");
    db.init(K_EAP_USER,     "");
    db.init(K_EAP_PASS,     "");
    db.init(K_EAP_CA_CERT,  "");
    db.init(K_API_URL,      "http://192.168.1.100:8000");
    db.init(K_API_QUEUE,    "");
    db.init(K_API_TOKEN,    "");
    db.init(K_API_SECRET,   "");
    db.init(K_BT_NAME,      DEFAULT_PRINTER_NAME);
    db.init(K_BT_MAC,       "");
    db.init(K_WEB_PASS,     "");
    db.init(K_CONFIGURED,   (uint8_t)0);

    sett.onBuild(buildUI);
    sett.onUpdate(updateUI);

    String savedPass = db[K_WEB_PASS].toString();
    if (savedPass.length()) sett.setPass(savedPass.c_str());

    bool configured = (bool)db[K_CONFIGURED].toInt();
    if (!configured || db[K_SSID].toString().isEmpty()) {
        startAP();
    } else {
        doConnect();
        if (!wifiConnected) startAP();
    }

    sett.begin();
    Serial.printf("[HTTP] http://%s\n",
        wifiConnected ? WiFi.localIP().toString().c_str()
                      : WiFi.softAPIP().toString().c_str());

    // ВАЖНО: BLEDevice::init() вызывать НЕ НУЖНО —
    // tpScan() делает это внутри себя
    if (configured) connectPrinter();

    Serial.println("[Setup] Done.");
}

// =============================================================================
//  LOOP
// =============================================================================
void loop() {
    sett.server.handleClient();
    sett.tick();

    // Автопереподключение принтера раз в 30 сек
    if (!printerConnected) {
        static unsigned long lastRetry = 0;
        if (millis() - lastRetry > 30000UL) {
            lastRetry = millis();
            connectPrinter();
        }
    }

    if (wifiConnected && millis() - lastPoll >= POLL_INTERVAL_MS) {
        lastPoll = millis();
        pollServer();
    }

    yield();
}
