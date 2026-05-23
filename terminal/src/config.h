#pragma once
/**
 * ============================================================================
 *  config.h — константы, ключи БД, определения плат
 * ============================================================================
 */

#include <Arduino.h>
#include <GyverDBFile.h>

// ─── Версия ──────────────────────────────────────────────────────────────────
#define FW_VERSION  "2.2.0"

// ─── Принтер FunnyPrint ─────────────────────────────────────────────────────
#define PRINTER_WIDTH_PX      384
#define PRINTER_WIDTH_BYTES   48
#define BLE_CHUNK_SIZE        20
#define BLE_CHUNK_DELAY_MS    12     // увеличено для стабильности
#define SPP_CHUNK_SIZE        256    // уменьшено для надёжности
#define SPP_CHUNK_DELAY_MS    5
#define RASTER_BLOCK_HEIGHT   64     // уменьшено: меньше блок = стабильнее
#define BT_CONNECT_RETRIES    3      // попытки подключения
#define BT_RETRY_DELAY_MS     1000

// ─── MAC по умолчанию ───────────────────────────────────────────────────────
static const char DEFAULT_PRINTER_MAC[] = "AA:BB:CC:DD:EE:FF";

// ─── Интервал опроса ─────────────────────────────────────────────────────────
#define POLL_INTERVAL_MS  5000UL

// ─── AP ──────────────────────────────────────────────────────────────────────
static const char AP_SSID[] = "SmartQueue-Setup";
static const char AP_PASS[] = "12345678";

// ─── BT имена ───────────────────────────────────────────────────────────────
static const char BT_NAME_CLASSIC[] = "SmartQueue";
static const char BT_NAME_BLE[]     = "SmartQueue-C3";

// ─── BLE NUS UUID ────────────────────────────────────────────────────────────
static const char NUS_SVC_UUID[] = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static const char NUS_RX_UUID[]  = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";

// ─── Плата ───────────────────────────────────────────────────────────────────
#if defined(BOARD_WROOM)
    #define BT_CLASSIC 1
#elif defined(BOARD_C3)
    #define BT_CLASSIC 0
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

// ─── Глобалы ─────────────────────────────────────────────────────────────────
extern bool        g_wifiConnected;
extern bool        g_printerConnected;
extern GyverDBFile g_db;