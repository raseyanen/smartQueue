#pragma once
#include <Arduino.h>
#include <GyverDBFile.h>

#define FW_VERSION  "2.3.0"

// ─── Принтер FunnyPrint (BLE) ───────────────────────────────────────────────
// Кастомный BLE UUID (НЕ NUS!)
// Сервис: 0000AE30-0000-1000-8000-00805F9B34FB (или ищем по характеристике)
// Характеристика записи: 0000AE01-0000-1000-8000-00805F9B34FB
static const char FP_WRITE_UUID[] = "0000ae01-0000-1000-8000-00805f9b34fb";

#define PRINTER_WIDTH_PX      384
#define PRINTER_WIDTH_BYTES   48     // 384 / 8
#define BLE_CHUNK_SIZE        20     // MTU без negotiation
#define BLE_CHUNK_DELAY_MS    10     // мс между чанками
#define BLE_LINE_DELAY_MS     5      // мс между строками растра

// Авто-реконнект
#define BT_AUTO_RECONNECT_MS  30000UL

// ─── Дефолты ─────────────────────────────────────────────────────────────────
static const char DEFAULT_PRINTER_MAC[] = "AA:BB:CC:DD:EE:FF";
#define POLL_INTERVAL_MS  5000UL

static const char AP_SSID[] = "SmartQueue-Setup";
static const char AP_PASS[] = "12345678";
static const char BT_NAME[] = "SmartQueue";

// ─── Плата ───────────────────────────────────────────────────────────────────
// Обе платы теперь используют BLE (принтер FunnyPrint = BLE-only)
#if defined(BOARD_WROOM)
    // ESP32 WROOM поддерживает BLE через NimBLE
    #define BOARD_NAME "WROOM-32"
#elif defined(BOARD_C3)
    #define BOARD_NAME "C3"
#else
    #error "Define -D BOARD_WROOM or -D BOARD_C3"
#endif

// ─── Ключи БД ───────────────────────────────────────────────────────────────
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
#define K_BT_MAC         "bt_mac"_h
#define K_WEB_PASS       "web_pass"_h
#define K_CONFIGURED     "configured"_h

extern bool        g_wifiConnected;
extern bool        g_printerConnected;
extern GyverDBFile g_db;