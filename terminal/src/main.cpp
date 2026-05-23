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

GyverDBFile g_db(&LittleFS, "/sq_config.db");
SettingsESP sett("SmartQueue Terminal", &g_db);

bool g_wifiConnected    = false;
bool g_printerConnected = false;

static unsigned long lastPoll     = 0;
static unsigned long lastBtCheck  = 0;
static unsigned long lastBtReconn = 0;
static bool          btShouldConnect = false;

static char ipBuf[20];

static void updateIpBuf() {
    IPAddress ip = g_wifiConnected ? WiFi.localIP() : WiFi.softAPIP();
    snprintf(ipBuf, sizeof(ipBuf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

// =============================================================================
void setup() {
    Serial.begin(115200);
    delay(400);

    log_init();
    LOGI("MAIN", "=== SmartQueue v%s ===", FW_VERSION);
    LOGI("MAIN", "Board: %s (BLE)", BOARD_NAME);
    LOGI("MAIN", "Printer: FunnyPrint BLE (AE01)");
    LOGI("MAIN", "Heap: %u", ESP.getFreeHeap());

    if (!LittleFS.begin(true)) {
        LOGE("MAIN", "LittleFS FAIL");
    } else {
        LOGI("MAIN", "LittleFS OK");
    }

    db_init();

    sett.onBuild(ui_build);
    sett.onUpdate(ui_update);

    {
        char pw[64] = {0};
        auto v = g_db[K_WEB_PASS];
        strncpy(pw, v.toString().c_str(), sizeof(pw) - 1);
        if (pw[0]) sett.setPass(pw);
    }

    bool configured = (bool)g_db[K_CONFIGURED].toInt();
    char ssidTest[4] = {0};
    { auto v = g_db[K_SSID]; strncpy(ssidTest, v.toString().c_str(), 3); }

    if (!configured || !ssidTest[0]) {
        LOGI("MAIN", "AP mode");
        wifi_start_ap();
    } else {
        wifi_connect();
        if (!g_wifiConnected) {
            LOGW("MAIN", "WiFi fail -> AP");
            wifi_start_ap();
        }
    }

    sett.begin();

    sett.server.onNotFound([]() {
        sett.server.send(404, "text/plain", "404");
    });

    ui_register_log_endpoint();

    updateIpBuf();
    LOGI("MAIN", "Web: http://%s", ipBuf);
    LOGI("MAIN", "Log: http://%s/log", ipBuf);

    if (configured) {
        char macCheck[20] = {0};
        { auto v = g_db[K_BT_MAC]; strncpy(macCheck, v.toString().c_str(), 19); }

        if (strcmp(macCheck, DEFAULT_PRINTER_MAC) == 0 || strlen(macCheck) < 17) {
            LOGW("MAIN", "MAC is default - set in web UI");
        } else {
            btShouldConnect = true;
            LOGI("MAIN", "Connecting printer %s", macCheck);
            bt_connect();
            if (g_printerConnected) {
                raster_set_concentration(4);
                raster_print_line("SmartQueue v2.3", 1, 1, true);
                raster_print_line("FunnyPrint BLE", 1, 1, false);
                raster_print_line("Ready", 1, 1, false);
                raster_feed(2);
            }
        }
    }

    if (!configured && ssidTest[0]) {
        g_db[K_CONFIGURED] = (uint8_t)1;
    }

    LOGI("MAIN", "Done. Heap: %u", ESP.getFreeHeap());
}

// =============================================================================
void loop() {
    sett.server.handleClient();
    sett.tick();

    if (g_wifiConnected && millis() - lastPoll >= POLL_INTERVAL_MS) {
        lastPoll = millis();
        api_poll();
    }

    if (millis() - lastBtCheck >= 15000UL) {
        lastBtCheck = millis();
        if (g_printerConnected && !bt_is_alive()) {
            LOGW("MAIN", "Printer link lost");
            g_printerConnected = false;
        }
    }

    if (btShouldConnect && !g_printerConnected &&
        millis() - lastBtReconn >= BT_AUTO_RECONNECT_MS) {
        lastBtReconn = millis();
        LOGI("MAIN", "Auto-reconnect...");
        bt_connect();
    }

    yield();
}