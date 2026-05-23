#include "web_ui.h"
#include "wifi_manager.h"
#include "bt_printer.h"
#include "api_client.h"
#include "logger.h"
#include <WiFi.h>

#define TAG "UI"

extern SettingsESP sett;

static char _ipBuf[20];
static char _upBuf[16];
static char _heapBuf[12];

static const char* ipLabel() {
    if (g_wifiConnected) {
        IPAddress ip = WiFi.localIP();
        snprintf(_ipBuf, sizeof(_ipBuf), "%u.%u.%u.%u", ip[0],ip[1],ip[2],ip[3]);
    } else {
        IPAddress ip = WiFi.softAPIP();
        snprintf(_ipBuf, sizeof(_ipBuf), "%u.%u.%u.%u", ip[0],ip[1],ip[2],ip[3]);
    }
    return _ipBuf;
}

static const char* uptimeLabel() {
    unsigned long s = millis() / 1000;
    snprintf(_upBuf, sizeof(_upBuf), "%lu:%02lu:%02lu", s/3600, (s/60)%60, s%60);
    return _upBuf;
}

static const char* heapLabel() {
    snprintf(_heapBuf, sizeof(_heapBuf), "%u", ESP.getFreeHeap());
    return _heapBuf;
}

static const char* btStatusLabel() {
    if (!g_printerConnected) return "Disconnected";
    if (bt_is_alive())       return "Connected OK";
    return "Connected (stale?)";
}

// Последние 5 строк лога
static char _lastLogBuf[620];
static const char* lastLogLines() {
    const char* full = log_get_text();
    if (!full || !full[0] || strcmp(full, "(empty)") == 0) {
        return "(no entries)";
    }

    int totalLines = 0;
    for (const char* p = full; *p; p++) {
        if (*p == '\n') totalLines++;
    }

    int skip = totalLines - 5;
    if (skip < 0) skip = 0;

    const char* p = full;
    int line = 0;
    while (*p && line < skip) {
        if (*p == '\n') line++;
        p++;
    }

    strncpy(_lastLogBuf, p, sizeof(_lastLogBuf) - 1);
    _lastLogBuf[sizeof(_lastLogBuf) - 1] = '\0';
    return _lastLogBuf;
}

// =============================================================================
void ui_register_log_endpoint() {
    sett.server.on("/log", HTTP_GET, []() {
        const char* logText = log_get_text();

        String page;
        page.reserve(strlen(logText) + 600);
        page += F("<!DOCTYPE html><html><head>"
                   "<meta charset='utf-8'>"
                   "<meta http-equiv='refresh' content='5'>"
                   "<title>SmartQueue Log</title>"
                   "<style>"
                   "body{background:#1a1a2e;color:#e0e0e0;"
                   "font-family:monospace;font-size:13px;padding:16px;}"
                   "pre{background:#16213e;padding:12px;border-radius:8px;"
                   "white-space:pre-wrap;word-wrap:break-word;"
                   "max-height:85vh;overflow-y:auto;}"
                   "a{color:#e94560;background:#0f3460;padding:8px 16px;"
                   "text-decoration:none;border-radius:4px;margin-right:8px;}"
                   ".info{color:#888;font-size:11px;margin:8px 0;}"
                   "</style></head><body>"
                   "<h2>System Log</h2>"
                   "<div>"
                   "<a href='/log'>Refresh</a>"
                   "<a href='/log/clear'>Clear</a>"
                   "<a href='/'>Settings</a></div>"
                   "<div class='info'>Auto: 5s | Lines: ");
        page += String(log_count());
        page += F(" | Heap: ");
        page += String(ESP.getFreeHeap());
        page += F("</div><pre>");
        page += logText;
        page += F("</pre></body></html>");

        sett.server.send(200, "text/html", page);
    });

    sett.server.on("/log/clear", HTTP_GET, []() {
        log_clear();
        LOGI("UI", "Log cleared via /log/clear");
        sett.server.sendHeader("Location", "/log");
        sett.server.send(302);
    });

    sett.server.on("/log/raw", HTTP_GET, []() {
        sett.server.send(200, "text/plain", log_get_text());
    });

    LOGI(TAG, "Log endpoints registered: /log /log/clear /log/raw");
}

// =============================================================================
void ui_build(sets::Builder& b) {

    // Статус
    {
        sets::GuestAccess ga(b);
        sets::Group g(b, "Status");
        b.Label("lbl_ip"_h,    "IP",       ipLabel());
        b.Label("lbl_bt"_h,    "Printer",  btStatusLabel());
        b.Label("lbl_fw"_h,    "Firmware", FW_VERSION);
        b.Label("lbl_up"_h,    "Uptime",   uptimeLabel());
        b.Label("lbl_heap"_h,  "Heap",     heapLabel());
    }

    // WiFi
    {
        sets::Menu m(b, "WiFi");
        b.Select(K_WIFI_MODE, "Mode", "Personal (PSK);Enterprise (EAP)");
        b.Input(K_SSID, "SSID");

        { sets::Group g(b, "Personal");
          b.Pass(K_PSK, "Password", ""); }

        { sets::Group g(b, "Enterprise");
          b.Select(K_EAP_METHOD, "Method",
                   "PEAP-MSCHAPv2;EAP-TTLS-PAP;EAP-TLS");
          b.Input(K_EAP_IDENTITY, "Outer identity");
          b.Input(K_EAP_USER,     "Username");
          b.Pass (K_EAP_PASS,     "Password", "");
          b.Input(K_EAP_CA_CERT,  "CA cert (PEM)"); }

        { sets::Buttons btns(b);
          if (b.Button("btn_wf"_h, "Save & Reconnect", sets::Colors::Mint)) {
              LOGI(TAG, "WiFi reconnect");
              WiFi.disconnect(true);
              g_wifiConnected = false;
              delay(300);
              wifi_connect();
              b.reload();
          }
        }
    }

    // API
    {
        sets::Menu m(b, "API Server");
        b.Input(K_API_URL,   "URL (http://host:port)");
        b.Input(K_API_QUEUE, "Queue UUID");

        { sets::Group g(b, "Auth");
          b.Pass(K_API_TOKEN,  "Bearer token", "");
          b.Pass(K_API_SECRET, "HMAC secret",  ""); }

        { sets::Buttons btns(b);
          if (b.Button("btn_at"_h, "Test API")) {
              LOGI(TAG, "API test");
              api_poll();
          }
        }
    }

    // Принтер
    {
        sets::Menu m(b, "Printer");
        b.Input(K_BT_MAC, "MAC (XX:XX:XX:XX:XX:XX)");

#if BT_CLASSIC
        b.Label("lbl_btm"_h, "Interface", "Classic SPP");
#else
        b.Label("lbl_btm"_h, "Interface", "BLE NUS");
#endif
        b.Label("lbl_bts"_h, "Status", btStatusLabel());

        { sets::Buttons btns(b);
          if (b.Button("btn_bc"_h, "Connect", sets::Colors::Blue)) {
              LOGI(TAG, "Connect printer");
              // НЕ вызываем bt_disconnect() если стек не запущен
              if (bt_is_alive()) {
                  bt_disconnect();
                  delay(500);
              }
              bt_connect();
              b.reload();
          }
          if (b.Button("btn_bd"_h, "Disconnect", sets::Colors::Red)) {
              LOGI(TAG, "Disconnect printer");
              if (bt_is_alive()) {
                  bt_disconnect();
              } else {
                  g_printerConnected = false;
                  LOGI(TAG, "Was not connected");
              }
              b.reload();
          }
          if (b.Button("btn_tp"_h, "Test print", sets::Colors::Mint)) {
              LOGI(TAG, "Test print");
              if (!g_printerConnected || !bt_is_alive()) {
                  LOGW(TAG, "Not connected, connecting...");
                  bt_connect();
              }
              if (g_printerConnected) {
                  printTicket("A001", "TEST QUEUE", "~0 min");
              } else {
                  LOGE(TAG, "Print failed: no connection");
              }
          }
        }
    }

    // Логи
    {
        sets::Menu m(b, "System Log");
        b.Label("lbl_logcnt"_h, "Entries", String(log_count()).c_str());
        b.Label("lbl_log1"_h,   "Recent",  lastLogLines());
        b.Label("lbl_logurl"_h, "Full log", "Open /log in browser");

        { sets::Buttons btns(b);
          if (b.Button("btn_logr"_h, "Refresh")) { b.reload(); }
          if (b.Button("btn_logc"_h, "Clear", sets::Colors::Red)) {
              log_clear();
              LOGI(TAG, "Log cleared");
              b.reload();
          }
        }
    }

    // Security
    {
        sets::Menu m(b, "Security");
        if (b.Pass(K_WEB_PASS, "Web password", "")) {
            char pw[64] = {0};
            auto v = g_db[K_WEB_PASS];
            strncpy(pw, v.toString().c_str(), sizeof(pw)-1);
            sett.setPass(pw);
            LOGI(TAG, "Password set");
        }
    }

    // System
    {
        sets::Group g(b, "System");
        { sets::Buttons btns(b);
          if (b.Button("btn_rb"_h, "Reboot", sets::Colors::Red)) {
              LOGI(TAG, "Reboot");
              delay(500);
              ESP.restart();
          }
        }
    }
}

// =============================================================================
void ui_update(sets::Updater& upd) {
    upd.update("lbl_ip"_h,       ipLabel());
    upd.update("lbl_bt"_h,       btStatusLabel());
    upd.update("lbl_bts"_h,      btStatusLabel());
    upd.update("lbl_up"_h,       uptimeLabel());
    upd.update("lbl_heap"_h,     heapLabel());
    upd.update("lbl_logcnt"_h,   String(log_count()).c_str());
}