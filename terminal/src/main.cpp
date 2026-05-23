/**
 * ============================================================================
 *  SmartQueue Terminal v2.2 — FunnyPrint Raster + Logging
 * ============================================================================
 */

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
#define BT_CHECK_INTERVAL_MS  10000UL

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

    // 1. Логирование — ПЕРВЫМ
    log_init();
    LOGI("MAIN", "=== SmartQueue Terminal v%s ===", FW_VERSION);
#if BT_CLASSIC
    LOGI("MAIN", "Board: WROOM-32 (Classic SPP)");
#else
    LOGI("MAIN", "Board: C3 (BLE NUS)");
#endif
    LOGI("MAIN", "Raster width: %d px", PRINTER_WIDTH_PX);
    LOGI("MAIN", "Free heap: %u", ESP.getFreeHeap());

    // 2. LittleFS
    if (!LittleFS.begin(true)) {
        LOGE("MAIN", "LittleFS FAIL");
    } else {
        LOGI("MAIN", "LittleFS OK");
    }

    // 3. БД
    db_init();

    // 4. Settings callbacks
    sett.onBuild(ui_build);
    sett.onUpdate(ui_update);

    // Пароль
    {
        char pw[64] = {0};
        auto v = g_db[K_WEB_PASS];
        strncpy(pw, v.toString().c_str(), sizeof(pw)-1);
        if (pw[0]) {
            sett.setPass(pw);
            LOGD("MAIN", "Web password restored");
        }
    }

    // 5. WiFi
    bool configured = (bool)g_db[K_CONFIGURED].toInt();
    char ssidTest[4] = {0};
    { auto v = g_db[K_SSID]; strncpy(ssidTest, v.toString().c_str(), 3); }

    if (!configured || !ssidTest[0]) {
        LOGI("MAIN", "No config / no SSID -> AP mode");
        wifi_start_ap();
    } else {
        wifi_connect();
        if (!g_wifiConnected) {
            LOGW("MAIN", "WiFi failed -> AP fallback");
            wifi_start_ap();
        }
    }

    // 6. Settings begin (HTTP server start)
    sett.begin();

    // 7. Регистрируем эндпоинты логов ПОСЛЕ sett.begin()
    ui_register_log_endpoint();

    if (g_wifiConnected) {
        LOGI("MAIN", "Web UI: http://%s", WiFi.localIP().toString().c_str());
        LOGI("MAIN", "Full log: http://%s/log", WiFi.localIP().toString().c_str());
    } else {
        LOGI("MAIN", "Web UI: http://%s", WiFi.softAPIP().toString().c_str());
        LOGI("MAIN", "Full log: http://%s/log", WiFi.softAPIP().toString().c_str());
    }

    // 8. Принтер
    if (configured) {
        LOGI("MAIN", "Connecting printer...");
        bool btOk = bt_connect();
        if (btOk && g_printerConnected) {
            LOGI("MAIN", "Printing startup banner");
            raster_print_line("SmartQueue v2.2", 1, 1, true);
            raster_print_line("Raster Ready", 1, 1, false);
            raster_feed(2);
            LOGI("MAIN", "Banner printed OK");
        } else {
            LOGW("MAIN", "Printer not available at startup");
        }
    } else {
        LOGI("MAIN", "Not configured, skipping printer");
    }

    // 9. configured flag
    if (!configured && ssidTest[0]) {
        g_db[K_CONFIGURED] = (uint8_t)1;
        LOGD("MAIN", "Marked as configured");
    }

    LOGI("MAIN", "Setup complete. Heap: %u", ESP.getFreeHeap());
}

// =============================================================================
void loop() {
    sett.server.handleClient();
    sett.tick();

    // API polling
    if (g_wifiConnected && millis() - lastPoll >= POLL_INTERVAL_MS) {
        lastPoll = millis();
        api_poll();
    }

    // BT health check
    if (millis() - lastBtCheck >= BT_CHECK_INTERVAL_MS) {
        lastBtCheck = millis();
        checkBtConnection();
    }

    yield();
}