#include "web_ui.h"
#include "wifi_manager.h"
#include "bt_printer.h"
#include "api_client.h"
#include "logger.h"
#include <WiFi.h>

#define TAG "UI"
extern SettingsESP sett;

static char _ipBuf[20], _upBuf[16], _heapBuf[12];

static const char* ipLabel() {
    IPAddress ip = g_wifiConnected ? WiFi.localIP() : WiFi.softAPIP();
    snprintf(_ipBuf, sizeof(_ipBuf), "%u.%u.%u.%u", ip[0],ip[1],ip[2],ip[3]);
    return _ipBuf;
}
static const char* uptimeLabel() {
    unsigned long s = millis()/1000;
    snprintf(_upBuf, sizeof(_upBuf), "%lu:%02lu:%02lu", s/3600, (s/60)%60, s%60);
    return _upBuf;
}
static const char* heapLabel() {
    snprintf(_heapBuf, sizeof(_heapBuf), "%u", ESP.getFreeHeap());
    return _heapBuf;
}
static const char* btLabel() {
    if (!g_printerConnected) return "Disconnected";
    return bt_is_alive() ? "Connected OK" : "Stale?";
}

static char _logBuf[620];
static const char* lastLogLines() {
    const char* f = log_get_text();
    if (!f || !f[0] || strcmp(f,"(empty)")==0) return "(no entries)";
    int n=0; for(const char* p=f;*p;p++) if(*p=='\n') n++;
    int skip=n-5; if(skip<0) skip=0;
    const char* p=f; int l=0;
    while(*p && l<skip) { if(*p=='\n') l++; p++; }
    strncpy(_logBuf, p, sizeof(_logBuf)-1);
    _logBuf[sizeof(_logBuf)-1]='\0';
    return _logBuf;
}

void ui_register_log_endpoint() {
    sett.server.on("/log", HTTP_GET, []() {
        const char* t = log_get_text();
        String page; page.reserve(strlen(t)+600);
        page += F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
                   "<meta http-equiv='refresh' content='5'><title>Log</title>"
                   "<style>body{background:#1a1a2e;color:#e0e0e0;"
                   "font-family:monospace;font-size:13px;padding:16px;}"
                   "pre{background:#16213e;padding:12px;border-radius:8px;"
                   "white-space:pre-wrap;max-height:85vh;overflow-y:auto;}"
                   "a{color:#fff;background:#0f3460;padding:8px 16px;"
                   "text-decoration:none;border-radius:4px;margin-right:8px;}"
                   ".i{color:#888;font-size:11px;margin:8px 0;}"
                   "</style></head><body><h2>Log</h2>"
                   "<div><a href='/log'>Refresh</a>"
                   "<a href='/log/clear'>Clear</a>"
                   "<a href='/'>Settings</a></div>"
                   "<div class='i'>");
        page += String(log_count());
        page += F(" entries | Heap: ");
        page += String(ESP.getFreeHeap());
        page += F("</div><pre>");
        page += t;
        page += F("</pre></body></html>");
        sett.server.send(200, "text/html", page);
    });

    sett.server.on("/log/clear", HTTP_GET, []() {
        log_clear(); LOGI("UI","Cleared");
        sett.server.sendHeader("Location","/log");
        sett.server.send(302);
    });

    sett.server.on("/log/raw", HTTP_GET, []() {
        sett.server.send(200, "text/plain", log_get_text());
    });

    LOGI(TAG, "Endpoints: /log /log/clear /log/raw");
}

void ui_build(sets::Builder& b) {
    { sets::GuestAccess ga(b); sets::Group g(b, "Status");
      b.Label("lbl_ip"_h,   "IP",       ipLabel());
      b.Label("lbl_bt"_h,   "Printer",  btLabel());
      b.Label("lbl_fw"_h,   "Firmware", FW_VERSION);
      b.Label("lbl_bd"_h,   "Board",    BOARD_NAME);
      b.Label("lbl_pr"_h,   "Protocol", "FunnyPrint BLE (AE01)");
      b.Label("lbl_up"_h,   "Uptime",   uptimeLabel());
      b.Label("lbl_hp"_h,   "Heap",     heapLabel());
    }

    { sets::Menu m(b, "WiFi");
      b.Select(K_WIFI_MODE, "Mode", "Personal;Enterprise");
      b.Input(K_SSID, "SSID");
      { sets::Group g(b, "Personal"); b.Pass(K_PSK, "Password", ""); }
      { sets::Group g(b, "Enterprise");
        b.Select(K_EAP_METHOD, "Method", "PEAP;TTLS-PAP;EAP-TLS");
        b.Input(K_EAP_IDENTITY, "Identity");
        b.Input(K_EAP_USER, "Username");
        b.Pass(K_EAP_PASS, "Password", "");
        b.Input(K_EAP_CA_CERT, "CA cert"); }
      { sets::Buttons btns(b);
        if (b.Button("bwf"_h, "Save & Reconnect", sets::Colors::Mint)) {
            LOGI(TAG,"WiFi"); WiFi.disconnect(true);
            g_wifiConnected=false; delay(300); wifi_connect(); b.reload();
        }
      }
    }

    { sets::Menu m(b, "API Server");
      b.Input(K_API_URL, "URL"); b.Input(K_API_QUEUE, "Queue UUID");
      { sets::Group g(b, "Auth");
        b.Pass(K_API_TOKEN, "Token", "");
        b.Pass(K_API_SECRET, "HMAC", ""); }
      { sets::Buttons btns(b);
        if (b.Button("bat"_h, "Test")) { LOGI(TAG,"API"); api_poll(); }
      }
    }

    { sets::Menu m(b, "Printer");
      b.Input(K_BT_MAC, "MAC (XX:XX:XX:XX:XX:XX)");
      b.Label("lbs"_h, "Status", btLabel());
      b.Label("lbp"_h, "Write UUID", FP_WRITE_UUID);
      { sets::Buttons btns(b);
        if (b.Button("bbc"_h, "Connect", sets::Colors::Blue)) {
            LOGI(TAG,"Connect"); bt_connect(); b.reload();
        }
        if (b.Button("bbd"_h, "Disconnect", sets::Colors::Red)) {
            LOGI(TAG,"Disconnect");
            if (bt_is_alive()) bt_disconnect();
            else g_printerConnected=false;
            b.reload();
        }
        if (b.Button("btp"_h, "Test print", sets::Colors::Mint)) {
            LOGI(TAG,"Test");
            if (!g_printerConnected) bt_connect();
            if (g_printerConnected) printTicket("A001","TEST","~0 min");
            else LOGE(TAG,"No conn");
        }
      }
    }

    { sets::Menu m(b, "Log");
      b.Label("llc"_h, "Entries", String(log_count()).c_str());
      b.Label("ll1"_h, "Recent", lastLogLines());
      b.Label("llu"_h, "Full", "Open /log");
      { sets::Buttons btns(b);
        if (b.Button("blr"_h, "Refresh")) b.reload();
        if (b.Button("blc"_h, "Clear", sets::Colors::Red)) {
            log_clear(); LOGI(TAG,"Cleared"); b.reload();
        }
      }
    }

    { sets::Menu m(b, "Security");
      if (b.Pass(K_WEB_PASS, "Password", "")) {
          char pw[64]={0}; auto v=g_db[K_WEB_PASS];
          strncpy(pw,v.toString().c_str(),63); sett.setPass(pw);
      }
    }

    { sets::Group g(b, "System");
      { sets::Buttons btns(b);
        if (b.Button("brb"_h, "Reboot", sets::Colors::Red)) {
            LOGI(TAG,"Reboot"); delay(500); ESP.restart();
        }
      }
    }
}

void ui_update(sets::Updater& upd) {
    upd.update("lbl_ip"_h, ipLabel());
    upd.update("lbl_bt"_h, btLabel());
    upd.update("lbs"_h,    btLabel());
    upd.update("lbl_up"_h, uptimeLabel());
    upd.update("lbl_hp"_h, heapLabel());
    upd.update("llc"_h,    String(log_count()).c_str());
}