/**
 * ============================================================================
 *  main.cpp — SmartQueue Terminal v2.1 (FunnyPrint Raster)
 * ============================================================================
 */

#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>

#include <GyverDBFile.h>
#include <SettingsESP.h>

#include "config.h"
#include "database.h"
#include "wifi_manager.h"
#include "bt_printer.h"
#include "api_client.h"
#include "web_ui.h"

// =============================================================================
//  Глобальные объекты (extern в config.h)
// =============================================================================
GyverDBFile g_db(&LittleFS, "/sq_config.db");
SettingsESP sett("SmartQueue Terminal", &g_db);

bool g_wifiConnected    = false;
bool g_printerConnected = false;

// Bluetooth объекты (extern в bt_printer.cpp)
#if BT_CLASSIC
    #include <BluetoothSerial.h>
    BluetoothSerial SerialBT;
#else
    #include <NimBLEDevice.h>
    NimBLEClient*                pBleClient = nullptr;
    NimBLERemoteCharacteristic*  pBleRxChar = nullptr;
#endif

static unsigned long lastPoll = 0;

// =============================================================================
//  SETUP
// =============================================================================
void setup() {
    Serial.begin(115200);
    delay(400);

    Serial.println(F("\n=== SmartQueue Terminal v2.1 (FunnyPrint Raster) ==="));
#if BT_CLASSIC
    Serial.println(F("Board: WROOM-32  BT: Classic SPP"));
#else
    Serial.println(F("Board: C3  BT: BLE NUS"));
#endif
    Serial.printf("Printer width: %d px\n", PRINTER_WIDTH_PX);

    // 1. Файловая система
    if (!LittleFS.begin(true)) {
        Serial.println(F("[FS] LittleFS FAIL"));
    }

    // 2. БД
    db_init();

    // 3. Веб-интерфейс
    sett.onBuild(ui_build);
    sett.onUpdate(ui_update);

    // Пароль из БД
    {
        char pw[64] = {0};
        auto v = g_db[K_WEB_PASS];
        strncpy(pw, v.toString().c_str(), sizeof(pw)-1);
        if (pw[0]) sett.setPass(pw);
    }

    // 4. WiFi
    bool configured = (bool)g_db[K_CONFIGURED].toInt();
    char ssidTest[4] = {0};
    { auto v = g_db[K_SSID]; strncpy(ssidTest, v.toString().c_str(), 3); }

    if (!configured || !ssidTest[0]) {
        Serial.println(F("[Setup] AP mode"));
        wifi_start_ap();
    } else {
        wifi_connect();
        if (!g_wifiConnected) {
            Serial.println(F("[Setup] WiFi fail -> AP"));
            wifi_start_ap();
        }
    }

    // 5. Settings begin (HTTP + WS)
    sett.begin();

    if (g_wifiConnected) {
        Serial.printf("[HTTP] http://%s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.printf("[HTTP] http://%s\n", WiFi.softAPIP().toString().c_str());
    }

    // 6. Принтер
    if (configured) {
        bt_connect();
        if (g_printerConnected) {
            raster_print_line("SmartQueue v2.1", 1, 1, true);
            raster_print_line("FunnyPrint Raster", 1, 1, false);
            raster_print_line("Ready", 1, 1, false);
            raster_feed(2);
        }
    }

    // 7. Флаг configured
    if (!configured && ssidTest[0]) {
        g_db[K_CONFIGURED] = (uint8_t)1;
    }

    Serial.println(F("[Setup] Done"));
}

// =============================================================================
//  LOOP
// =============================================================================
void loop() {
    sett.server.handleClient();
    sett.tick();

    if (g_wifiConnected && millis() - lastPoll >= POLL_INTERVAL_MS) {
        lastPoll = millis();
        api_poll();
    }

    yield();
}