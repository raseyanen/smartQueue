#pragma once
/**
 * ============================================================================
 *  config.h — константы, ключи БД, определения плат
 * ============================================================================
 */

#include <Arduino.h>
#include <GyverDBFile.h>   // для hash-литералов _h

// ─── Версия прошивки ─────────────────────────────────────────────────────────
#define FW_VERSION  "2.1.0"

// ─── Параметры принтера FunnyPrint ──────────────────────────────────────────
#define PRINTER_WIDTH_PX      384
#define PRINTER_WIDTH_BYTES   48      // 384 / 8
#define BLE_CHUNK_SIZE        20
#define BLE_CHUNK_DELAY_MS    8
#define SPP_CHUNK_SIZE        512
#define SPP_CHUNK_DELAY_MS    2
#define RASTER_BLOCK_HEIGHT   128     // строк за одну отправку

// ─── MAC по умолчанию ───────────────────────────────────────────────────────
static const char DEFAULT_PRINTER_MAC[] = "AA:BB:CC:DD:EE:FF";

// ─── Интервал опроса ─────────────────────────────────────────────────────────
#define POLL_INTERVAL_MS  5000UL

// ─── AP по умолчанию ─────────────────────────────────────────────────────────
static const char AP_SSID[] = "SmartQueue-Setup";
static const char AP_PASS[] = "12345678";

// ─── Имена BT-устройств ─────────────────────────────────────────────────────
static const char BT_NAME_CLASSIC[] = "SmartQueue";
static const char BT_NAME_BLE[]     = "SmartQueue-C3";

// ─── BLE NUS UUID ────────────────────────────────────────────────────────────
static const char NUS_SVC_UUID[] = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static const char NUS_RX_UUID[]  = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";

// ─── Выбор платы ─────────────────────────────────────────────────────────────
#if defined(BOARD_WROOM)
    #define BT_CLASSIC 1
#elif defined(BOARD_C3)
    #define BT_CLASSIC 0
#else
    #error "Укажите -D BOARD_WROOM или -D BOARD_C3 в platformio.ini"
#endif

// =============================================================================
//  Ключи GyverDB  (hash-литералы)
// =============================================================================
// WiFi
#define K_WIFI_MODE      "wifi_mode"_h
#define K_SSID           "ssid"_h
#define K_PSK            "psk"_h
// Enterprise
#define K_EAP_METHOD     "eap_method"_h
#define K_EAP_IDENTITY   "eap_id"_h
#define K_EAP_USER       "eap_user"_h
#define K_EAP_PASS       "eap_pass"_h
#define K_EAP_CA_CERT    "eap_ca"_h
// API
#define K_API_URL        "api_url"_h
#define K_API_QUEUE      "api_queue"_h
#define K_API_TOKEN      "api_token"_h
#define K_API_SECRET     "api_secret"_h
// Принтер
#define K_BT_MAC         "bt_mac"_h
// Веб
#define K_WEB_PASS       "web_pass"_h
// Служебный
#define K_CONFIGURED     "configured"_h

// =============================================================================
//  Глобальные флаги (extern — определены в main.cpp)
// =============================================================================
extern bool     g_wifiConnected;
extern bool     g_printerConnected;
extern GyverDBFile g_db;