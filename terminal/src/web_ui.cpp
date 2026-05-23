#include "web_ui.h"
#include "wifi_manager.h"
#include "bt_printer.h"
#include "api_client.h"
#include <WiFi.h>

// ── Глобалы определены в main.cpp ───────────────────────────────────────────
extern SettingsESP sett;

// ── Вспомогательный буфер для меток ─────────────────────────────────────────
static char _lblBuf[32];

static const char* ipLabel() {
    if (g_wifiConnected) {
        IPAddress ip = WiFi.localIP();
        snprintf(_lblBuf, sizeof(_lblBuf), "%u.%u.%u.%u",
                 ip[0], ip[1], ip[2], ip[3]);
    } else {
        strcpy(_lblBuf, "No connection");
    }
    return _lblBuf;
}

static const char* uptimeLabel() {
    snprintf(_lblBuf, sizeof(_lblBuf), "%lu", millis() / 1000);
    return _lblBuf;
}

// =============================================================================
void ui_build(sets::Builder& b) {

    // ── Статус ───────────────────────────────────────────────────────────
    {
        sets::GuestAccess ga(b);
        sets::Group g(b, "Status");

        b.Label("lbl_ip"_h,    "IP",       ipLabel());
        b.Label("lbl_bt"_h,    "Printer",  g_printerConnected ? "Connected" : "Disconnected");
        b.Label("lbl_fw"_h,    "Firmware", FW_VERSION);
        b.Label("lbl_proto"_h, "Protocol", "Raster (FunnyPrint)");
        b.Label("lbl_up"_h,    "Uptime",   uptimeLabel());
    }

    // ── WiFi ─────────────────────────────────────────────────────────────
    {
        sets::Menu m(b, "WiFi");

        b.Select(K_WIFI_MODE, "Mode", "Personal (PSK);Enterprise (EAP)");
        b.Input(K_SSID, "SSID");

        {
            sets::Group g(b, "Personal (WPA/WPA2-PSK)");
            b.Pass(K_PSK, "Password", "");
        }
        {
            sets::Group g(b, "Enterprise (802.1X)");
            b.Select(K_EAP_METHOD, "EAP method",
                     "PEAP-MSCHAPv2;EAP-TTLS-PAP;EAP-TLS");
            b.Input(K_EAP_IDENTITY, "Outer identity");
            b.Input(K_EAP_USER,     "Username");
            b.Pass (K_EAP_PASS,     "Password", "");
            b.Input(K_EAP_CA_CERT,  "CA cert (PEM)");
        }
        {
            sets::Buttons btns(b);
            if (b.Button("btn_wf"_h, "Save & Reconnect", sets::Colors::Mint)) {
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

        {
            sets::Group g(b, "Auth");
            b.Pass(K_API_TOKEN,  "Bearer token",  "");
            b.Pass(K_API_SECRET, "HMAC secret",    "");
        }
        {
            sets::Buttons btns(b);
            if (b.Button("btn_at"_h, "Test connection")) {
                api_poll();
            }
        }
    }

    // ── Принтер ──────────────────────────────────────────────────────────
    {
        sets::Menu m(b, "Printer (FunnyPrint)");

        b.Input(K_BT_MAC, "MAC (XX:XX:XX:XX:XX:XX)");

#if BT_CLASSIC
        b.Label("lbl_btm"_h, "Interface", "Classic SPP (WROOM)");
#else
        b.Label("lbl_btm"_h, "Interface", "BLE NUS (C3)");
#endif
        b.Label("lbl_p2"_h, "Protocol", "Raster 384px (1D 76 30)");

        {
            sets::Buttons btns(b);
            if (b.Button("btn_bc"_h, "Reconnect printer", sets::Colors::Blue)) {
                bt_disconnect();
                delay(200);
                bt_connect();
                b.reload();
            }
            if (b.Button("btn_tp"_h, "Test print")) {
                printTicket("A001", "TEST QUEUE", "~0 min");
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
            Serial.println(F("[Auth] Password set"));
        }
    }

    // ── Система ──────────────────────────────────────────────────────────
    {
        sets::Group g(b, "System");
        {
            sets::Buttons btns(b);
            if (b.Button("btn_rb"_h, "Reboot", sets::Colors::Red)) {
                delay(500);
                ESP.restart();
            }
        }
    }
}

// =============================================================================
void ui_update(sets::Updater& upd) {
    upd.update("lbl_ip"_h,  ipLabel());
    upd.update("lbl_bt"_h,  g_printerConnected ? "Connected" : "Disconnected");
    upd.update("lbl_up"_h,  uptimeLabel());
}