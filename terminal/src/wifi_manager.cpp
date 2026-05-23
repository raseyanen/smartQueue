#include "wifi_manager.h"
#include "logger.h"
#include <WiFi.h>
#include <esp_wpa2.h>

#define TAG "WiFi"

static bool connectPSK(const char* ssid, const char* psk) {
    LOGI(TAG, "PSK -> %s", ssid);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, psk);
    for (int i = 0; i < 40; i++) {
        if (WiFi.status() == WL_CONNECTED) break;
        delay(500);
        if (i % 5 == 0) LOGD(TAG, "Waiting... (%d/40)", i);
    }
    bool ok = (WiFi.status() == WL_CONNECTED);
    if (ok) LOGI(TAG, "PSK connected");
    else    LOGE(TAG, "PSK connect failed (status=%d)", WiFi.status());
    return ok;
}

static bool connectEnterprise(const char* ssid, int method,
                              const char* identity, const char* user,
                              const char* pass, const char* caCert) {
    LOGI(TAG, "EAP -> %s method=%d", ssid, method);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    esp_wifi_sta_wpa2_ent_disable();

    if (identity && identity[0]) {
        esp_wifi_sta_wpa2_ent_set_identity((uint8_t*)identity, strlen(identity));
        LOGD(TAG, "Identity: %s", identity);
    }

    if (method == 2) {
        LOGI(TAG, "EAP-TLS mode");
        if (caCert && caCert[0])
            esp_wifi_sta_wpa2_ent_set_ca_cert((uint8_t*)caCert, strlen(caCert)+1);
    } else {
        if (user && user[0]) {
            esp_wifi_sta_wpa2_ent_set_username((uint8_t*)user, strlen(user));
            LOGD(TAG, "User: %s", user);
        }
        if (pass && pass[0])
            esp_wifi_sta_wpa2_ent_set_password((uint8_t*)pass, strlen(pass));
        if (caCert && caCert[0])
            esp_wifi_sta_wpa2_ent_set_ca_cert((uint8_t*)caCert, strlen(caCert)+1);
        if (method == 1) {
            esp_wifi_sta_wpa2_ent_set_ttls_phase2_method(ESP_EAP_TTLS_PHASE2_PAP);
            LOGD(TAG, "TTLS phase2: PAP");
        }
    }

    esp_wifi_sta_wpa2_ent_enable();
    WiFi.begin(ssid);
    for (int i = 0; i < 60; i++) {
        if (WiFi.status() == WL_CONNECTED) break;
        delay(500);
        if (i % 5 == 0) LOGD(TAG, "EAP waiting... (%d/60)", i);
    }
    bool ok = (WiFi.status() == WL_CONNECTED);
    if (ok) LOGI(TAG, "EAP connected");
    else    LOGE(TAG, "EAP failed (status=%d)", WiFi.status());
    return ok;
}

void wifi_connect() {
    int mode = (int)g_db[K_WIFI_MODE].toInt();

    char ssidBuf[64] = {0};
    { auto v = g_db[K_SSID]; strncpy(ssidBuf, v.toString().c_str(), sizeof(ssidBuf)-1); }

    if (!ssidBuf[0]) {
        LOGW(TAG, "SSID empty, cannot connect");
        return;
    }

    bool ok = false;
    if (mode == 0) {
        char pskBuf[64] = {0};
        { auto v = g_db[K_PSK]; strncpy(pskBuf, v.toString().c_str(), sizeof(pskBuf)-1); }
        ok = connectPSK(ssidBuf, pskBuf);
    } else {
        char idBuf[128]={0}, userBuf[128]={0}, passBuf[128]={0}, caBuf[2048]={0};
        int eapM = (int)g_db[K_EAP_METHOD].toInt();
        { auto v = g_db[K_EAP_IDENTITY]; strncpy(idBuf,   v.toString().c_str(), sizeof(idBuf)-1);   }
        { auto v = g_db[K_EAP_USER];     strncpy(userBuf, v.toString().c_str(), sizeof(userBuf)-1); }
        { auto v = g_db[K_EAP_PASS];     strncpy(passBuf, v.toString().c_str(), sizeof(passBuf)-1); }
        { auto v = g_db[K_EAP_CA_CERT];  strncpy(caBuf,   v.toString().c_str(), sizeof(caBuf)-1);   }
        ok = connectEnterprise(ssidBuf, eapM, idBuf, userBuf, passBuf, caBuf);
    }

    g_wifiConnected = ok;
    if (ok) {
        LOGI(TAG, "IP: %s", WiFi.localIP().toString().c_str());
    }
}

void wifi_start_ap() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    LOGI(TAG, "AP started: %s pw=%s IP=%s",
         AP_SSID, AP_PASS, WiFi.softAPIP().toString().c_str());
}