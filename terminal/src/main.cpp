/**
 * ============================================================================
 *  SmartQueue Terminal  v2
 * ============================================================================
 *  Веб-интерфейс настроек через GyverLibs/Settings (правильный API v1.x):
 *    • WiFi: Personal (WPA/WPA2-PSK) + Enterprise (PEAP/TTLS/EAP-TLS)
 *    • API-сервер: URL, UUID очереди, API-токен + HMAC-секрет для подписи
 *    • Bluetooth-принтер FunnyPrint LX-D02: MAC-адрес
 *    • OTA: встроенное в Settings (файловый менеджер + загрузка прошивки)
 *    • Авторизация веб-интерфейса паролем (гостевой/админ режим)
 *    • Динамические метки статуса (IP, BT, Uptime)
 *
 *  Платы:
 *    esp32_wroom       → Bluetooth Classic SPP  (BluetoothSerial)
 *    esp32c3_supermini → BLE Nordic UART Service (NimBLE)
 * ============================================================================
 */

#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WebServer.h>
#include <esp_wpa2.h>          // WPA2 Enterprise
#include <esp_ota_ops.h>       // Версия прошивки из OTA-раздела

// GyverLibs
#include <GyverDBFile.h>       // GyverDB + автосохранение в LittleFS
#include <SettingsESP.h>       // Settings на стандартном esp-WebServer

// ─── Bluetooth ───────────────────────────────────────────────────────────────
#if defined(BOARD_WROOM)
    #include <BluetoothSerial.h>
    #if !defined(CONFIG_BT_SPP_ENABLED)
        #error "Classic BT SPP не включён"
    #endif
    BluetoothSerial SerialBT;
    #define BT_CLASSIC 1
#elif defined(BOARD_C3)
    #include <NimBLEDevice.h>
    #define NUS_SVC  "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
    #define NUS_RX   "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
    NimBLEClient*                pBleClient = nullptr;
    NimBLERemoteCharacteristic*  pBleRxChar = nullptr;
    #define BT_CLASSIC 0
#else
    #error "Укажите -D BOARD_WROOM или -D BOARD_C3 в platformio.ini"
#endif

// ─── Версия прошивки ─────────────────────────────────────────────────────────
#define FW_VERSION  "2.0.0"

// ─── MAC-адрес принтера по умолчанию (замените своим) ───────────────────────
#define DEFAULT_PRINTER_MAC  "AA:BB:CC:DD:EE:FF"

// ─── Интервал опроса API-сервера ─────────────────────────────────────────────
#define POLL_INTERVAL_MS  5000UL

// =============================================================================
//  Ключи GyverDB  (hash-литералы, экономим RAM)
// =============================================================================
// ── WiFi ─────────────────────────────────────────────────────────────────────
#define K_WIFI_MODE      "wifi_mode"_h      // 0=PSK, 1=Enterprise
#define K_SSID           "ssid"_h
#define K_PSK            "psk"_h
// Enterprise
#define K_EAP_METHOD     "eap_method"_h     // 0=PEAP, 1=TTLS, 2=TLS
#define K_EAP_IDENTITY   "eap_id"_h
#define K_EAP_USER       "eap_user"_h
#define K_EAP_PASS       "eap_pass"_h
#define K_EAP_CA_CERT    "eap_ca"_h         // PEM-строка CA-сертификата (опц.)
// ── API-сервер ────────────────────────────────────────────────────────────────
#define K_API_URL        "api_url"_h        // http://host:port
#define K_API_QUEUE      "api_queue"_h      // UUID очереди
#define K_API_TOKEN      "api_token"_h      // Bearer-токен
#define K_API_SECRET     "api_secret"_h     // HMAC-секрет для подписи запросов
// ── Принтер ──────────────────────────────────────────────────────────────────
#define K_BT_MAC         "bt_mac"_h
// ── Веб-интерфейс ────────────────────────────────────────────────────────────
#define K_WEB_PASS       "web_pass"_h       // Пароль веб-интерфейса
// ── Служебный флаг ───────────────────────────────────────────────────────────
#define K_CONFIGURED     "configured"_h

// =============================================================================
//  Глобальные объекты
// =============================================================================
// SettingsESP создаёт WebServer(80) внутри себя — не передаём свой
GyverDBFile  db(&LittleFS, "/sq_config.db");
SettingsESP  sett("SmartQueue Terminal", &db);

bool  wifiConnected    = false;
bool  printerConnected = false;
unsigned long lastPoll = 0;

// =============================================================================
//  ESC/POS через Bluetooth
// =============================================================================
static void btWrite(const uint8_t* d, size_t n) {
#if BT_CLASSIC
    if (SerialBT.connected()) SerialBT.write(d, n);
#else
    if (printerConnected && pBleRxChar) {
        for (size_t off = 0; off < n; off += 20) {
            size_t chunk = min((size_t)20, n - off);
            pBleRxChar->writeValue(d + off, chunk, true);
            delay(8);
        }
    }
#endif
}
static void btStr(const char* s)  { btWrite((const uint8_t*)s, strlen(s)); }
static void btByte(uint8_t b)     { btWrite(&b, 1); }

namespace ESC {
    void init()               { uint8_t c[]={0x1B,0x40}; btWrite(c,2); delay(30); }
    void feed(uint8_t n=3)    { uint8_t c[]={0x1B,0x64,n}; btWrite(c,3); }
    void align(uint8_t a)     { uint8_t c[]={0x1B,0x61,a}; btWrite(c,3); }
    void bold(bool on)        { uint8_t c[]={0x1B,0x45,(uint8_t)(on?1:0)}; btWrite(c,3); }
    void dblSize(bool on)     { uint8_t c[]={0x1D,0x21,(uint8_t)(on?0x11:0x00)}; btWrite(c,3); }
    void println(const char* s){ btStr(s); btByte(0x0A); }
    void separator()          { println("------------------------"); }
}

void printTicket(const String& num, const String& queue, const String& eta) {
    if (!printerConnected) { Serial.println("[Printer] not connected"); return; }
    ESC::init();
    ESC::align(1); ESC::dblSize(true); ESC::bold(true);
    ESC::println("SMART QUEUE");
    ESC::dblSize(false); ESC::bold(false);
    ESC::separator();
    ESC::println(queue.c_str());
    ESC::dblSize(true); ESC::bold(true);
    ESC::println(num.c_str());
    ESC::dblSize(false); ESC::bold(false);
    if (eta.length()) { String s="Wait: "+eta; ESC::align(0); ESC::println(s.c_str()); }
    ESC::separator();
    ESC::feed(4);
    Serial.printf("[Printer] Ticket %s printed\n", num.c_str());
}

// =============================================================================
//  Bluetooth-подключение
// =============================================================================
#if BT_CLASSIC
bool connectBT() {
    String mac = db[K_BT_MAC].toString();
    if (mac.length() < 17) mac = DEFAULT_PRINTER_MAC;
    uint8_t addr[6];
    if (sscanf(mac.c_str(),"%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
               &addr[0],&addr[1],&addr[2],&addr[3],&addr[4],&addr[5]) != 6) {
        Serial.println("[BT] Bad MAC");
        return false;
    }
    SerialBT.end();
    delay(200);
    if (!SerialBT.begin("SmartQueue", true)) return false;
    bool ok = SerialBT.connect(addr);
    printerConnected = ok;
    if (ok) { delay(200); ESC::init(); Serial.println("[BT] Connected"); }
    else      Serial.println("[BT] Failed");
    return ok;
}
#else
class BleCallbacks : public NimBLEClientCallbacks {
    void onDisconnect(NimBLEClient*) override {
        printerConnected = false;
        Serial.println("[BLE] Disconnected");
    }
};
bool connectBT() {
    String mac = db[K_BT_MAC].toString();
    if (mac.length() < 17) mac = DEFAULT_PRINTER_MAC;
    NimBLEDevice::init("SmartQueue-C3");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    pBleClient = NimBLEDevice::createClient();
    pBleClient->setClientCallbacks(new BleCallbacks, false);
    if (!pBleClient->connect(NimBLEAddress(mac.c_str(), BLE_ADDR_PUBLIC))) {
        Serial.println("[BLE] Connect failed"); return false;
    }
    auto* svc = pBleClient->getService(NUS_SVC);
    if (!svc) { pBleClient->disconnect(); Serial.println("[BLE] No NUS svc"); return false; }
    pBleRxChar = svc->getCharacteristic(NUS_RX);
    if (!pBleRxChar) { pBleClient->disconnect(); Serial.println("[BLE] No RX char"); return false; }
    printerConnected = true;
    delay(200); ESC::init();
    Serial.println("[BLE] Connected");
    return true;
}
#endif

// =============================================================================
//  WiFi — Personal (PSK)
// =============================================================================
bool connectWifiPSK(const String& ssid, const String& psk) {
    Serial.printf("[WiFi] PSK → %s\n", ssid.c_str());
    WiFi.disconnect(true); WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), psk.c_str());
    for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) {
        delay(500); Serial.print('.');
    }
    Serial.println();
    return WiFi.status() == WL_CONNECTED;
}

// =============================================================================
//  WiFi — Enterprise (802.1X / EAP)
//
//  Метод:  0 = PEAP-MSCHAPv2   (самый распространённый, Eduroam)
//          1 = EAP-TTLS-PAP
//          2 = EAP-TLS          (только сертификаты, без пароля)
//
//  Identity  = анонимная (outer) идентификация, напр. "anonymous@domain"
//  Username  = настоящее имя пользователя (inner/MSCHAPV2)
//  Password  = пароль пользователя
//  CA cert   = PEM-строка корневого сертификата RADIUS (опционально,
//              но рекомендуется для предотвращения MITM)
// =============================================================================
bool connectWifiEnterprise(const String& ssid,
                           int           method,
                           const String& identity,
                           const String& username,
                           const String& password,
                           const String& caCert) {
    Serial.printf("[WiFi] Enterprise → %s (method=%d)\n", ssid.c_str(), method);

    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);

    // Чистим предыдущие EAP-настройки
    esp_wifi_sta_wpa2_ent_disable();

    // Внешняя идентификация (anonymous outer identity)
    if (identity.length()) {
        esp_wifi_sta_wpa2_ent_set_identity(
            (uint8_t*)identity.c_str(), identity.length());
    }

    if (method == 2) {
        // EAP-TLS: сертификаты, пароль не используется
        // CA cert
        if (caCert.length()) {
            esp_wifi_sta_wpa2_ent_set_ca_cert(
                (uint8_t*)caCert.c_str(), caCert.length() + 1);
        }
        // Клиентский сертификат и ключ здесь не реализованы —
        // их нужно прошивать отдельно через спец. раздел NVS.
        Serial.println("[WiFi] EAP-TLS: client cert/key must be set via NVS");
    } else {
        // PEAP или TTLS — нужны имя пользователя + пароль
        if (username.length()) {
            esp_wifi_sta_wpa2_ent_set_username(
                (uint8_t*)username.c_str(), username.length());
        }
        if (password.length()) {
            esp_wifi_sta_wpa2_ent_set_password(
                (uint8_t*)password.c_str(), password.length());
        }
        if (caCert.length()) {
            esp_wifi_sta_wpa2_ent_set_ca_cert(
                (uint8_t*)caCert.c_str(), caCert.length() + 1);
        }
        if (method == 1) {
            // TTLS: переключаем phase2 на PAP
            esp_wifi_sta_wpa2_ent_set_ttls_phase2_method(
                ESP_EAP_TTLS_PHASE2_PAP);
        }
        // method == 0 → PEAP-MSCHAPv2 (дефолт, ничего дополнительно)
    }

    // arduino-esp32 v2.0+: esp_wifi_sta_wpa2_ent_enable() без аргументов
    // (esp_wpa2_config_t и WPA2_CONFIG_INIT_DEFAULT удалены из нового SDK)
    esp_wifi_sta_wpa2_ent_enable();

    WiFi.begin(ssid.c_str());

    for (int i = 0; i < 60 && WiFi.status() != WL_CONNECTED; i++) {
        delay(500); Serial.print('.');
    }
    Serial.println();
    return WiFi.status() == WL_CONNECTED;
}

// =============================================================================
//  Единая точка подключения к WiFi
// =============================================================================
void doConnect() {
    int   mode     = (int)db[K_WIFI_MODE].toInt();
    String ssid    = db[K_SSID].toString();
    bool ok = false;

    if (ssid.isEmpty()) {
        Serial.println("[WiFi] SSID not set, starting AP");
        return;
    }

    if (mode == 0) {
        ok = connectWifiPSK(ssid, db[K_PSK].toString());
    } else {
        ok = connectWifiEnterprise(
            ssid,
            (int)db[K_EAP_METHOD].toInt(),
            db[K_EAP_IDENTITY].toString(),
            db[K_EAP_USER].toString(),
            db[K_EAP_PASS].toString(),
            db[K_EAP_CA_CERT].toString()
        );
    }

    if (ok) {
        wifiConnected = true;
        Serial.printf("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        wifiConnected = false;
        Serial.println("[WiFi] Failed");
    }
}

void startAP() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("SmartQueue-Setup", "12345678");
    Serial.printf("[WiFi] AP: SmartQueue-Setup  IP: %s\n",
                  WiFi.softAPIP().toString().c_str());
}

// =============================================================================
//  Простой HMAC-SHA256 через mbedTLS для подписи API-запросов
//  Подпись передаётся в заголовке X-Signature: HMAC-SHA256 <hex>
// =============================================================================
#include <mbedtls/md.h>

String hmacSha256(const String& key, const String& data) {
    uint8_t out[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&ctx,
        (const uint8_t*)key.c_str(), key.length());
    mbedtls_md_hmac_update(&ctx,
        (const uint8_t*)data.c_str(), data.length());
    mbedtls_md_hmac_finish(&ctx, out);
    mbedtls_md_free(&ctx);

    String hex; hex.reserve(64);
    for (int i = 0; i < 32; i++) {
        char buf[3];
        snprintf(buf, 3, "%02x", out[i]);
        hex += buf;
    }
    return hex;
}

// =============================================================================
//  Опрос API-сервера
// =============================================================================
#include <WiFiClient.h>
#include <HTTPClient.h>

void pollServer() {
    String apiUrl    = db[K_API_URL].toString();
    String queueId   = db[K_API_QUEUE].toString();
    String apiToken  = db[K_API_TOKEN].toString();
    String apiSecret = db[K_API_SECRET].toString();

    if (!wifiConnected || apiUrl.isEmpty() || queueId.isEmpty()) return;

    String url = apiUrl + "/api/queues/" + queueId + "/next_ticket/";
    Serial.printf("[API] GET %s\n", url.c_str());

    WiFiClient   wc;
    HTTPClient   http;
    http.begin(wc, url);

    // Bearer-токен
    if (apiToken.length()) {
        http.addHeader("Authorization", "Token " + apiToken);
    }

    // HMAC-подпись: подписываем метод + URL
    if (apiSecret.length()) {
        String msg = "GET:" + url;
        String sig = hmacSha256(apiSecret, msg);
        http.addHeader("X-Signature", "HMAC-SHA256 " + sig);
    }

    http.addHeader("Content-Type", "application/json");
    int code = http.GET();
    Serial.printf("[API] %d\n", code);

    if (code == 200) {
        String body = http.getString();
        Serial.printf("[API] %s\n", body.c_str());

        // Примитивный JSON-парсинг без зависимости ArduinoJson
        auto jField = [](const String& j, const String& k) -> String {
            String sk = "\"" + k + "\":\"";
            int s = j.indexOf(sk);
            if (s < 0) return "";
            s += sk.length();
            int e = j.indexOf('"', s);
            return (e < 0) ? "" : j.substring(s, e);
        };

        String num   = jField(body, "number");
        String queue = jField(body, "queue");
        String eta   = jField(body, "eta");
        if (num.isEmpty()) num = body;
        if (queue.isEmpty()) queue = "Queue";
        printTicket(num, queue, eta);
    }
    // 204/404 → нет талонов, игнорируем
    http.end();
}

// =============================================================================
//  Построитель веб-интерфейса Settings
// =============================================================================
void buildUI(sets::Builder& b) {

    // ── Статус (только чтение, виден гостям) ─────────────────────────────
    {
        sets::GuestAccess ga(b);           // виден без пароля
        sets::Group g(b, "Статус");

        b.Label("lbl_ip"_h,   "IP-адрес",
            wifiConnected ? WiFi.localIP().toString().c_str() : "Нет подключения");
        b.Label("lbl_bt"_h,   "Принтер",
            printerConnected ? "Подключён" : "Отключён");
        b.Label("lbl_fw"_h,   "Прошивка", FW_VERSION);
        b.Label("lbl_up"_h,   "Uptime (с)",
            String(millis() / 1000).c_str());
    }

    // ── WiFi ─────────────────────────────────────────────────────────────
    {
        sets::Menu m(b, "WiFi");

        b.Select(K_WIFI_MODE, "Режим", "Personal (PSK);Enterprise (EAP)");

        b.Input(K_SSID, "SSID");

        // Personal
        {
            sets::Group g(b, "Personal (WPA/WPA2-PSK)");
            b.Pass(K_PSK, "Пароль сети", "••••••••");
        }

        // Enterprise
        {
            sets::Group g(b, "Enterprise (802.1X)");
            b.Select(K_EAP_METHOD, "EAP-метод",
                     "PEAP-MSCHAPv2 (Eduroam);EAP-TTLS-PAP;EAP-TLS (сертификат)");
            b.Input(K_EAP_IDENTITY, "Outer identity (напр. anonymous@domain)");
            b.Input(K_EAP_USER,     "Username (inner)");
            b.Pass (K_EAP_PASS,     "Password", "••••••••");
            b.Input(K_EAP_CA_CERT,  "CA-сертификат (PEM, опционально)");
        }

        {
            sets::Buttons btns(b);
            if (b.Button("btn_wifi_save"_h, "Сохранить и переподключить",
                         sets::Colors::Mint)) {
                Serial.println("[WiFi] Reconnect requested");
                WiFi.disconnect(true);
                wifiConnected = false;
                delay(300);
                doConnect();
                b.reload();
            }
        }
    }

    // ── API-сервер ────────────────────────────────────────────────────────
    {
        sets::Menu m(b, "API-сервер");

        b.Input(K_API_URL,   "URL сервера (http://host:port)");
        b.Input(K_API_QUEUE, "UUID очереди");

        {
            sets::Group g(b, "Аутентификация");
            b.Pass(K_API_TOKEN,  "Bearer-токен",  "••••••••");
            b.Pass(K_API_SECRET, "HMAC-секрет (X-Signature)", "••••••••");
        }

        {
            sets::Buttons btns(b);
            if (b.Button("btn_api_test"_h, "Тест соединения")) {
                Serial.println("[API] Manual poll requested");
                pollServer();
            }
        }
    }

    // ── Принтер ──────────────────────────────────────────────────────────
    {
        sets::Menu m(b, "Принтер Bluetooth");

        b.Input(K_BT_MAC, "MAC-адрес (XX:XX:XX:XX:XX:XX)");

#if BT_CLASSIC
        b.Label("lbl_btm"_h, "Интерфейс", "Classic SPP (WROOM)");
#else
        b.Label("lbl_btm"_h, "Интерфейс", "BLE NUS (C3)");
#endif

        {
            sets::Buttons btns(b);
            if (b.Button("btn_bt_connect"_h, "Переподключить принтер",
                         sets::Colors::Blue)) {
                printerConnected = false;
#if BT_CLASSIC
                SerialBT.disconnect(); delay(300);
#else
                if (pBleClient && pBleClient->isConnected()) {
                    pBleClient->disconnect(); delay(300);
                }
#endif
                connectBT();
                b.reload();
            }
            if (b.Button("btn_bt_test"_h, "Тест печати")) {
                printTicket("A001", "TEST QUEUE", "~0 min");
            }
        }
    }

    // ── Безопасность ─────────────────────────────────────────────────────
    {
        sets::Menu m(b, "Безопасность");
        b.Pass(K_WEB_PASS, "Пароль веб-интерфейса", "••••••••");

        if (b.Input(K_WEB_PASS, "Новый пароль")) {
            String np = db[K_WEB_PASS].toString();
            sett.setPass(np.c_str());
            Serial.println("[Auth] Password updated");
        }
    }

    // ── OTA + файловый менеджер ───────────────────────────────────────────
    // Settings встраивает OTA автоматически — кнопка появляется в правом
    // верхнем меню веб-интерфейса.  Дополнительных строк не требуется.

    // ── Перезагрузка ─────────────────────────────────────────────────────
    {
        sets::Group g(b, "Система");
        {
            sets::Buttons btns(b);
            if (b.Button("btn_reboot"_h, "Перезагрузить", sets::Colors::Red)) {
                Serial.println("[SYS] Reboot requested");
                delay(500);
                ESP.restart();
            }
        }
    }
}

// =============================================================================
//  Обновление динамических меток (вызывается периодически из onUpdate)
// =============================================================================
void updateUI(sets::Updater& upd) {
    upd.update("lbl_ip"_h,
        wifiConnected ? WiFi.localIP().toString().c_str() : "Нет подключения");
    upd.update("lbl_bt"_h,
        printerConnected ? "Подключён" : "Отключён");
    upd.update("lbl_up"_h,
        String(millis() / 1000).c_str());
}

// =============================================================================
//  SETUP
// =============================================================================
void setup() {
    Serial.begin(115200);
    delay(400);
    Serial.println("\n=== SmartQueue Terminal v2 ===");
#if BT_CLASSIC
    Serial.println("Board: WROOM-32  BT: Classic SPP");
#else
    Serial.println("Board: C3 SuperMini  BT: BLE NUS");
#endif

    // 1. Файловая система
    if (!LittleFS.begin(true)) {
        Serial.println("[FS] LittleFS FAIL — formatting...");
    } else {
        Serial.println("[FS] OK");
    }

    // 2. База данных
    db.begin();

    // 3. Инициализация значений по умолчанию
    //    db.init() записывает только если ключа ещё нет
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
    db.init(K_BT_MAC,       DEFAULT_PRINTER_MAC);
    db.init(K_WEB_PASS,     "");
    db.init(K_CONFIGURED,   (uint8_t)0);

    // 4. Веб-интерфейс
    sett.onBuild(buildUI);
    sett.onUpdate(updateUI);

    // Восстанавливаем пароль веб-интерфейса из БД
    String savedPass = db[K_WEB_PASS].toString();
    if (savedPass.length()) sett.setPass(savedPass.c_str());

    // 5. WiFi
    bool configured = (bool)db[K_CONFIGURED].toInt();
    String ssid     = db[K_SSID].toString();

    if (!configured || ssid.isEmpty()) {
        Serial.println("[Setup] First run → AP mode");
        startAP();
    } else {
        doConnect();
        if (!wifiConnected) {
            Serial.println("[Setup] WiFi failed → fallback AP");
            startAP();
        }
    }

    // 6. Settings (регистрирует маршруты + вызывает server.begin())
    sett.begin();
    Serial.printf("[HTTP] Web UI → http://%s\n",
        wifiConnected ? WiFi.localIP().toString().c_str()
                      : WiFi.softAPIP().toString().c_str());

    // 7. Принтер
    if (configured) {
        connectBT();
        if (printerConnected) {
            ESC::align(1); ESC::bold(true); ESC::println("SmartQueue v2");
            ESC::bold(false); ESC::println("Ready"); ESC::feed(2);
        }
    }

    // 8. Отмечаем что настройки применены (при первом сохранении данных
    //    пользователь должен нажать "Сохранить", после чего делается
    //    перезагрузка — и configured становится 1)
    if (!configured && !ssid.isEmpty()) {
        db[K_CONFIGURED] = (uint8_t)1;
    }

    Serial.println("[Setup] Done.");
}

// =============================================================================
//  LOOP
// =============================================================================
void loop() {
    sett.server.handleClient(); // HTTP-обработчик (WebServer встроен в SettingsESP)
    sett.tick();              // Settings: WebSocket, OTA, обновление виджетов

    // Опрос API
    if (wifiConnected && millis() - lastPoll >= POLL_INTERVAL_MS) {
        lastPoll = millis();
        pollServer();
    }

    yield();
}
