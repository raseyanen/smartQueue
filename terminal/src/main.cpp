#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>

#include <GyverDBFile.h>
#include <SettingsESP.h>

#include "config.h"
#include "logger.h"
#include "database.h"
#include "wifi_manager.h"
#include "bt_printer.h"
#include "api_client.h"
#include "web_ui.h"

// ── Глобалы ──────────────────────────────────────────────────────────────────
GyverDBFile g_db(&LittleFS, "/sq_config.db");
SettingsESP sett("SmartQueue Terminal", &g_db);

bool g_wifiConnected    = false;
bool g_printerConnected = false;

#if BT_CLASSIC
    #include <BluetoothSerial.h>
    BluetoothSerial SerialBT;
#else
    #include <NimBLEDevice.h>
    NimBLEClient*                pBleClient = nullptr;
    NimBLERemoteCharacteristic*  pBleRxChar = nullptr;
#endif

static unsigned long lastPoll    = 0;
static unsigned long lastBtCheck = 0;
#define BT_CHECK_INTERVAL_MS  15000UL

static void checkBtConnection() {
    if (!g_printerConnected) return;
    if (!bt_is_alive()) {
        LOGW("MAIN", "Printer connection lost");
        g_printerConnected = false;
    }
}

// =============================================================================
void setup() {
    Serial.begin(115200);
    delay(400);

    // 1. Логирование
    log_init();
    LOGI("MAIN", "=== SmartQueue Terminal v%s ===", FW_VERSION);
#if BT_CLASSIC
    LOGI("MAIN", "Board: WROOM-32 (Classic SPP)");
#else
    LOGI("MAIN", "Board: C3 (BLE NUS)");
#endif
    LOGI("MAIN", "Heap: %u", ESP.getFreeHeap());

    // 2. LittleFS
    if (!LittleFS.begin(true)) {
        LOGE("MAIN", "LittleFS FAIL");
    } else {
        LOGI("MAIN", "LittleFS OK");
    }

    // 3. БД
    db_init();

    // 4. Settings
    sett.onBuild(ui_build);
    sett.onUpdate(ui_update);

    {
        char pw[64] = {0};
        auto v = g_db[K_WEB_PASS];
        strncpy(pw, v.toString().c_str(), sizeof(pw) - 1);
        if (pw[0]) sett.setPass(pw);
    }

    // 5. WiFi
    bool configured = (bool)g_db[K_CONFIGURED].toInt();
    char ssidTest[4] = {0};
    { auto v = g_db[K_SSID]; strncpy(ssidTest, v.toString().c_str(), 3); }

    if (!configured || !ssidTest[0]) {
        LOGI("MAIN", "AP mode (not configured)");
        wifi_start_ap();
    } else {
        wifi_connect();
        if (!g_wifiConnected) {
            LOGW("MAIN", "WiFi fail -> AP fallback");
            wifi_start_ap();
        }
    }

    // 6. Settings begin
    sett.begin();

    // 7. Логи — ПОСЛЕ sett.begin()
    ui_register_log_endpoint();

    const char* ip = g_wifiConnected
        ? WiFi.localIP().toString().c_str()
        : WiFi.softAPIP().toString().c_str();
    LOGI("MAIN", "Web: http://%s", ip);
    LOGI("MAIN", "Log: http://%s/log", ip);

    // 8. Принтер — только если configured и MAC не дефолтный
    if (configured) {
        char macCheck[20] = {0};
        { auto v = g_db[K_BT_MAC]; strncpy(macCheck, v.toString().c_str(), 19); }

        if (strcmp(macCheck, DEFAULT_PRINTER_MAC) == 0 || strlen(macCheck) < 17) {
            LOGW("MAIN", "Printer MAC is default — skipping auto-connect");
            LOGW("MAIN", "Set correct MAC in web UI first");
        } else {
            LOGI("MAIN", "Auto-connecting printer %s ...", macCheck);
            bt_connect();
            if (g_printerConnected) {
                raster_print_line("SmartQueue v2.2", 1, 1, true);
                raster_print_line("Ready", 1, 1, false);
                raster_feed(2);
            }
        }
    }

    // 9. configured flag
    if (!configured && ssidTest[0]) {
        g_db[K_CONFIGURED] = (uint8_t)1;
    }

    LOGI("MAIN", "Setup done. Heap: %u", ESP.getFreeHeap());
}

// =============================================================================
void loop() {
    sett.server.handleClient();
    sett.tick();

    if (g_wifiConnected && millis() - lastPoll >= POLL_INTERVAL_MS) {
        lastPoll = millis();
        api_poll();
    }

    if (millis() - lastBtCheck >= BT_CHECK_INTERVAL_MS) {
        lastBtCheck = millis();
        checkBtConnection();
    }

    yield();
}