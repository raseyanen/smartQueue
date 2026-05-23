#include "database.h"

// Определение глобальной БД — в main.cpp (extern в config.h)

void db_init() {
    g_db.begin();

    g_db.init(K_WIFI_MODE,    (uint8_t)0);
    g_db.init(K_SSID,         "");
    g_db.init(K_PSK,          "");
    g_db.init(K_EAP_METHOD,   (uint8_t)0);
    g_db.init(K_EAP_IDENTITY, "anonymous");
    g_db.init(K_EAP_USER,     "");
    g_db.init(K_EAP_PASS,     "");
    g_db.init(K_EAP_CA_CERT,  "");
    g_db.init(K_API_URL,      "http://192.168.1.100:8000");
    g_db.init(K_API_QUEUE,    "");
    g_db.init(K_API_TOKEN,    "");
    g_db.init(K_API_SECRET,   "");
    g_db.init(K_BT_MAC,       DEFAULT_PRINTER_MAC);
    g_db.init(K_WEB_PASS,     "");
    g_db.init(K_CONFIGURED,   (uint8_t)0);
}