#include "web_ui.h"
#include "wifi_manager.h"
#include "bt_printer.h"
#include "api_client.h"
#include "logger.h"
#include <WiFi.h>

#define TAG "UI"

extern SettingsESP sett;

// ── Буферы для меток ────────────────────────────────────────────────────────
static char _ipBuf[20];
static char _upBuf[16];

static const char* ipLabel() {
    if (g_wifiConnected) {
        IPAddress ip = WiFi.localIP();
        snprintf(_ipBuf, sizeof(_ipBuf), "%u.%u.%u.%u", ip[0],ip[1],ip[2],ip[3]);
    } else {
        strcpy(_ipBuf, "No connection");
    }
    return _ipBuf;
}

static const char* uptimeLabel() {
    unsigned long s = millis() / 1000;
    unsigned long m = s / 60;
    unsigned long h = m / 60;
    snprintf(_upBuf, sizeof(_upBuf), "%lu:%02lu:%02lu", h, m%60, s%60);
    return _upBuf;
}

static const char* btStatusLabel() {
    if (!g_printerConnected) return "Disconnected";
    if (bt_is_alive())       return "Connected & alive";
    return "Connected (stale?)";
}

// ── Последние N строк лога для Label ─────────────────────────────────────────
// Label не поддерживает длинный текст, поэтому показываем последние 5 строк
static char _lastLogBuf[600];  // ~5 строк по 120 символов

static const char* lastLogLines() {
    const char* full = log_get_text();
    if (!full || full[0] == '\0' || strcmp(full, "(empty)") == 0) {
        return "(no log entries)";
    }

    // Находим последние 5 строк
    int totalLines = 0;
    const char* p = full;
    while (*p) {
        if (*p == '\n') totalLines++;
        p++;
    }

    int skip = totalLines - 5;
    if (skip < 0) skip = 0;

    p = full;
    int lineNum = 0;
    while (*p && lineNum < skip) {
        if (*p == '\n') lineNum++;
        p++;
    }

    strncpy(_lastLogBuf, p, sizeof(_lastLogBuf) - 1);
    _lastLogBuf[sizeof(_lastLogBuf) - 1] = '\0';
    return _lastLogBuf;
}

// =============================================================================
//  Регистрация HTTP-эндпоинта /log (полный лог как текстовая страница)
//  Вызывается один раз из main.cpp после sett.begin()
// =============================================================================
void ui_register_log_endpoint() {
    sett.server.on("/log", HTTP_GET, []() {
        const char* logText = log_get_text();

        // Простая HTML-страница с автообновлением
        String page;
        page.reserve(strlen(logText) + 512);
        page += F("<!DOCTYPE html><html><head>"
                   "<meta charset='utf-8'>"
                   "<meta http-equiv='refresh' content='5'>"
                   "<title>SmartQueue Log</title>"
                   "<style>"
                   "body{background:#1a1a2e;color:#e0e0e0;font-family:monospace;font-size:13px;padding:16px;}"
                   "pre{white-space:pre-wrap;word-wrap:break-word;background:#16213e;padding:12px;"
                   "border-radius:8px;max-height:85vh;overflow-y:auto;}"
                   "h2{color:#0f3460;} a{color:#e94560;}"
                   ".toolbar{margin-bottom:12px;}"
                   ".toolbar a{background:#0f3460;color:#fff;padding:8px 16px;"
                   "text-decoration:none;border-radius:4px;margin-right:8px;}"
                   ".toolbar a:hover{background:#e94560;}"
                   ".info{color:#888;font-size:11px;margin-bottom:8px;}"
                   "</style></head><body>"
                   "<h2>SmartQueue System Log</h2>"
                   "<div class='toolbar'>"
                   "<a href='/log'>Refresh</a>"
                   "<a href='/log/clear'>Clear</a>"
                   "<a href='/'>Back to Settings</a>"
                   "</div>"
                   "<div class='info'>Auto-refresh: 5s | Entries: ");
        page += String(log_count());
        page += F(" | Heap: ");
        page += String(ESP.getFreeHeap());
        page += F(" bytes</div><pre>");
        page += logText;
        page += F("</pre></body></html>");

        sett.server.send(200, "text/html", page);
    });

    // Эндпоинт очистки лога
    sett.server.on("/log/clear", HTTP_GET, []() {
        log_clear();
        LOGI("UI", "Log cleared via web");
        sett.server.sendHeader("Location", "/log");
        sett.server.send(302, "text/plain", "Redirecting...");
    });

    // Эндпоинт для получения raw-лога (для автоматизации)
    sett.server.on("/log/raw", HTTP_GET, []() {
        sett.server.send(200, "text/plain", log_get_text());
    });

    LOGI("UI", "Log endpoints registered: /log /log/clear /log/raw");
}

// =============================================================================
void ui_build(sets::Builder& b) {

    // ── Статус ───────────────────────────────────────────────────────────
    {
        sets::GuestAccess ga(b);
        sets::Group g(b, "Status");

        b.Label("lbl_ip"_h,    "IP",        ipLabel());
        b.Label("lbl_bt"_h,    "Printer",   btStatusLabel());
        b.Label("lbl_fw"_h,    "Firmware",  FW_VERSION);
        b.Label("lbl_proto"_h, "Protocol",  "Raster FunnyPrint 384px");
        b.Label("lbl_up"_h,    "Uptime",    uptimeLabel());
        b.Label("lbl_heap"_h,  "Free heap",
                String(ESP.getFreeHeap()).c_str());
    }

    // ── WiFi ─────────────────────────────────────────────────────────────
    {
        sets::Menu m(b, "WiFi");

        b.Select(K_WIFI_MODE, "Mode", "Personal (PSK);Enterprise (EAP)");
        b.Input(K_SSID, "SSID");

        { sets::Group g(b, "Personal");
          b.Pass(K_PSK, "Password", ""); }

        { sets::Group g(b, "Enterprise (802.1X)");
          b.Select(K_EAP_METHOD, "Method",
                   "PEAP-MSCHAPv2;EAP-TTLS-PAP;EAP-TLS");
          b.Input(K_EAP_IDENTITY, "Outer identity");
          b.Input(K_EAP_USER,     "Username");
          b.Pass (K_EAP_PASS,     "Password", "");
          b.Input(K_EAP_CA_CERT,  "CA cert (PEM)"); }

        { sets::Buttons btns(b);
          if (b.Button("btn_wf"_h, "Save & Reconnect", sets::Colors::Mint)) {
              LOGI(TAG, "WiFi reconnect requested");
              WiFi.disconnect(true);
              g_wifiConnected = false;
              delay(300);
              wifi_connect();
              b.reload();
          }
        }
    }

    // ── API ──────────────────────────────────────────────────────────────
    {
        sets::Menu m(b, "API Server");
        b.Input(K_API_URL,   "URL (http://host:port)");
        b.Input(K_API_QUEUE, "Queue UUID");

        { sets::Group g(b, "Auth");
          b.Pass(K_API_TOKEN,  "Bearer token", "");
          b.Pass(K_API_SECRET, "HMAC secret",  ""); }

        { sets::Buttons btns(b);
          if (b.Button("btn_at"_h, "Test API")) {
              LOGI(TAG, "Manual API test");
              api_poll();
          }
        }
    }

    // ── Принтер ──────────────────────────────────────────────────────────
    {
        sets::Menu m(b, "Printer (FunnyPrint)");

        b.Input(K_BT_MAC, "MAC (XX:XX:XX:XX:XX:XX)");

#if BT_CLASSIC
        b.Label("lbl_btm"_h, "Interface", "Classic SPP");
#else
        b.Label("lbl_btm"_h, "Interface", "BLE NUS (NimBLE)");
#endif
        b.Label("lbl_bts"_h, "Status", btStatusLabel());

        { sets::Buttons btns(b);
          if (b.Button("btn_bc"_h, "Connect printer", sets::Colors::Blue)) {
              LOGI(TAG, "Printer connect requested");
              bt_disconnect();
              delay(300);
              bt_connect();
              b.reload();
          }
          if (b.Button("btn_bd"_h, "Disconnect", sets::Colors::Red)) {
              LOGI(TAG, "Printer disconnect requested");
              bt_disconnect();
              b.reload();
          }
          if (b.Button("btn_tp"_h, "Test print", sets::Colors::Mint)) {
              LOGI(TAG, "Test print requested");
              if (!g_printerConnected) {
                  LOGW(TAG, "Not connected, trying first...");
                  bt_connect();
              }
              if (g_printerConnected) {
                  printTicket("A001", "TEST QUEUE", "~0 min");
              } else {
                  LOGE(TAG, "Cannot print: not connected");
              }
          }
        }
    }

    // ── Логи (превью + ссылка на полную страницу) ────────────────────────
    {
        sets::Menu m(b, "System Log");

        b.Label("lbl_logcnt"_h, "Entries", String(log_count()).c_str());

        // Последние 5 строк лога
        b.Label("lbl_log1"_h, "Recent log", lastLogLines());

        // Ссылка на полную страницу лога
        b.Label("lbl_logurl"_h, "Full log page",
                "Open /log in browser");

        { sets::Buttons btns(b);
          if (b.Button("btn_logr"_h, "Refresh")) {
              b.reload();
          }
          if (b.Button("btn_logc"_h, "Clear log", sets::Colors::Red)) {
              log_clear();
              LOGI(TAG, "Log cleared");
              b.reload();
          }
        }
    }

    // ── Безопасность ─────────────────────────────────────────────────────
    {
        sets::Menu m(b, "Security");
        if (b.Pass(K_WEB_PASS, "Web password", "")) {
            char pw[64] = {0};
            auto v = g_db[K_WEB_PASS];
            strncpy(pw, v.toString().c_str(), sizeof(pw)-1);
            sett.setPass(pw);
            LOGI(TAG, "Password updated");
        }
    }

    // ── Система ──────────────────────────────────────────────────────────
    {
        sets::Group g(b, "System");
        b.Label("lbl_heap2"_h, "Free heap",
                String(ESP.getFreeHeap()).c_str());

        { sets::Buttons btns(b);
          if (b.Button("btn_rb"_h, "Reboot", sets::Colors::Red)) {
              LOGI(TAG, "Reboot requested");
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
    upd.update("lbl_heap"_h,     String(ESP.getFreeHeap()).c_str());
    upd.update("lbl_heap2"_h,    String(ESP.getFreeHeap()).c_str());
    upd.update("lbl_logcnt"_h,   String(log_count()).c_str());
    upd.update("lbl_log1"_h,     lastLogLines());
}