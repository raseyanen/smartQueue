#include "wifi_manager.h"
#include "logger.h"
#include <WiFi.h>
#include <esp_wpa2.h>

#define TAG "WiFi"

static bool connectPSK(const char* ssid, const char* psk) {
    LOGI(TAG, "PSK -> %s", ssid);
    WiFi.disconnect(true); WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, psk);
    for (int i = 0; i < 40; i++) {
        if (WiFi.status() == WL_CONNECTED) break;
        delay(500);
    }
    bool ok = WiFi.status() == WL_CONNECTED;
    if (ok) LOGI(TAG, "Connected");
    else    LOGE(TAG, "Failed (%d)", WiFi.status());
    return ok;
}

static bool connectEAP(const char* ssid, int method,
                       const char* id, const char* user,
                       const char* pass, const char* ca) {
    LOGI(TAG, "EAP -> %s m=%d", ssid, method);
    WiFi.disconnect(true); WiFi.mode(WIFI_STA);
    esp_wifi_sta_wpa2_ent_disable();
    if (id && id[0])
        esp_wifi_sta_wpa2_ent_set_identity((uint8_t*)id, strlen(id));
    if (method == 2) {
        if (ca && ca[0])
            esp_wifi_sta_wpa2_ent_set_ca_cert((uint8_t*)ca, strlen(ca)+1);
    } else {
        if (user && user[0])
            esp_wifi_sta_wpa2_ent_set_username((uint8_t*)user, strlen(user));
        if (pass && pass[0])
            esp_wifi_sta_wpa2_ent_set_password((uint8_t*)pass, strlen(pass));
        if (ca && ca[0])
            esp_wifi_sta_wpa2_ent_set_ca_cert((uint8_t*)ca, strlen(ca)+1);
        if (method == 1)
            esp_wifi_sta_wpa2_ent_set_ttls_phase2_method(ESP_EAP_TTLS_PHASE2_PAP);
    }
    esp_wifi_sta_wpa2_ent_enable();
    WiFi.begin(ssid);
    for (int i = 0; i < 60; i++) {
        if (WiFi.status() == WL_CONNECTED) break;
        delay(500);
    }
    bool ok = WiFi.status() == WL_CONNECTED;
    if (ok) LOGI(TAG, "EAP OK"); else LOGE(TAG, "EAP fail");
    return ok;
}

void wifi_connect() {
    int mode = (int)g_db[K_WIFI_MODE].toInt();
    char ssid[64]={0};
    { auto v = g_db[K_SSID]; strncpy(ssid, v.toString().c_str(), 63); }
    if (!ssid[0]) { LOGW(TAG, "No SSID"); return; }

    bool ok = false;
    if (mode == 0) {
        char psk[64]={0};
        { auto v = g_db[K_PSK]; strncpy(psk, v.toString().c_str(), 63); }
        ok = connectPSK(ssid, psk);
    } else {
        char id[128]={0}, user[128]={0}, pass[128]={0}, ca[2048]={0};
        int em = (int)g_db[K_EAP_METHOD].toInt();
        { auto v = g_db[K_EAP_IDENTITY]; strncpy(id, v.toString().c_str(), 127); }
        { auto v = g_db[K_EAP_USER]; strncpy(user, v.toString().c_str(), 127); }
        { auto v = g_db[K_EAP_PASS]; strncpy(pass, v.toString().c_str(), 127); }
        { auto v = g_db[K_EAP_CA_CERT]; strncpy(ca, v.toString().c_str(), 2047); }
        ok = connectEAP(ssid, em, id, user, pass, ca);
    }
    g_wifiConnected = ok;
    if (ok) LOGI(TAG, "IP: %s", WiFi.localIP().toString().c_str());
}

void wifi_start_ap() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    LOGI(TAG, "AP: %s IP: %s", AP_SSID, WiFi.softAPIP().toString().c_str());
}