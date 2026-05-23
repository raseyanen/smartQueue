/*
 * SmartQueue Terminal - ESP32 (WROOM-32 / C3)
 * Uses: Settings, GyverPortal, FileData, GyverNTP
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <Update.h>
#include <LittleFS.h>
#include <Wire.h>
#include <GyverOLED.h>
#include <GyverButton.h>
#include <GSON.h>
#include <mString.h>
#include <GyverPortal.h>
#include <Settings.h>
#include <FileData.h>
#include <GyverNTP.h>

#define VERSION "2.0.0"
// Pin configuration
#define DEFAULT_BTN_PIN 15
#define DEFAULT_LED_PIN 2
#define DEFAULT_OLED_SDA 21
#define DEFAULT_OLED_SCL 22
#define PRINTER_RX 16
#define PRINTER_TX 17

GyverOLED<SSD1306_128x64, OLED_NO_BUFFER> oled;
GyverButton btn(DEFAULT_BTN_PIN);
WebServer server(80);
GyverNTP ntp;

// Settings builder
Settings settings("SmartQueue", "admin");

// Data storage using FileData
FileData fd("/smartqueue.dat");

struct Config {
    char ssid[32] = "";
    char password[64] = "";
    char serverUrl[64] = "http://192.168.1.100:8000";
    char terminalHash[32] = "";
    char queueLink[64] = "";
    char webPassword[32] = "admin";
    uint8_t btnPin = DEFAULT_BTN_PIN;
    uint8_t ledPin = DEFAULT_LED_PIN;
    uint8_t oledSda = DEFAULT_OLED_SDA;
    uint8_t oledScl = DEFAULT_OLED_SCL;
    bool oledEnabled = true;
} config;

uint8_t macAddress[6];
bool isApMode = false;
bool ticketPending = false;
int pendingTicketNumber = 0;
int pendingTicketId = 0;
unsigned long lastNtpSync = 0;
char currentTime[32] = "";

void oledInit() {
    if (!config.oledEnabled) return;
    Wire.begin(config.oledSda, config.oledScl);
    oled.init();
    oled.clear();
    oled.setCursor(0, 0);
    oled.print("SmartQueue");
    oled.setCursor(0, 20);
    oled.print("Terminal v");
    oled.print(VERSION);
    oled.update();
}

void oledShowStatus(const char* status) {
    if (!config.oledEnabled) return;
    oled.clear();
    oled.setCursor(0, 0);
    oled.print("SmartQueue");
    oled.setCursor(0, 20);
    oled.print(status);
    oled.update();
}

void oledShowTicket(int number, const char* type) {
    if (!config.oledEnabled) return;
    oled.clear();
    oled.setCursor(0, 0);
    oled.print("Talon: ");
    oled.print(type);
    oled.print(number);
    oled.setCursor(0, 30);
    oled.print("Press button");
    oled.update();
}

void ledBlink(int times, int interval) {
    for (int i = 0; i < times; i++) {
        digitalWrite(config.ledPin, HIGH);
        delay(interval);
        digitalWrite(config.ledPin, LOW);
        delay(interval);
    }
}

void getMacAddress() {
    esp_read_mac(macAddress, ESP_MAC_WIFI_STA);
}

mString generateHashFromMac() {
    mString hashInput = "";
    for (int i = 0; i < 6; i++) {
        if (macAddress[i] < 0x10) hashInput += "0";
        hashInput += String(macAddress[i], HEX);
    }
    uint32_t hash = 5381;
    for (int i = 0; i < hashInput.length(); i++) {
        hash = ((hash << 5) + hash) + hashInput.charAt(i);
    }
    mString result = String(hash, HEX);
    while (result.length() < 16) result = "0" + result;
    return result;
}

void loadConfigFromFile() {
    if (!fd.begin()) {
        Serial.println("FileData init failed");
        return;
    }
    fd.get("ssid", config.ssid, sizeof(config.ssid));
    fd.get("password", config.password, sizeof(config.password));
    fd.get("serverUrl", config.serverUrl, sizeof(config.serverUrl));
    fd.get("terminalHash", config.terminalHash, sizeof(config.terminalHash));
    fd.get("queueLink", config.queueLink, sizeof(config.queueLink));
    fd.get("webPassword", config.webPassword, sizeof(config.webPassword));
    config.btnPin = fd.getInt("btnPin", DEFAULT_BTN_PIN);
    config.ledPin = fd.getInt("ledPin", DEFAULT_LED_PIN);
    config.oledSda = fd.getInt("oledSda", DEFAULT_OLED_SDA);
    config.oledScl = fd.getInt("oledScl", DEFAULT_OLED_SCL);
    config.oledEnabled = fd.getBool("oledEnabled", true);
    Serial.println("Config loaded from FileData");
}

void saveConfigToFile() {
    if (!fd.begin()) {
        Serial.println("FileData init failed");
        return;
    }
    fd.put("ssid", config.ssid);
    fd.put("password", config.password);
    fd.put("serverUrl", config.serverUrl);
    fd.put("terminalHash", config.terminalHash);
    fd.put("queueLink", config.queueLink);
    fd.put("webPassword", config.webPassword);
    fd.putInt("btnPin", config.btnPin);
    fd.putInt("ledPin", config.ledPin);
    fd.putInt("oledSda", config.oledSda);
    fd.putInt("oledScl", config.oledScl);
    fd.putBool("oledEnabled", config.oledEnabled);
    Serial.println("Config saved to FileData");
}

void setupSettings() {
    settings.setAuth(config.webPassword);
    
    settings.begin("WiFi Settings");
    settings.text("ssid", "WiFi SSID", config.ssid, sizeof(config.ssid));
    settings.text("password", "WiFi Password", config.password, sizeof(config.password), "", true);
    settings.end();
    
    settings.begin("Server Settings");
    settings.text("serverUrl", "Server URL", config.serverUrl, sizeof(config.serverUrl));
    settings.text("queueLink", "Queue Link", config.queueLink, sizeof(config.queueLink));
    settings.end();
    
    settings.begin("Hardware");
    settings.number("btnPin", "Button Pin", config.btnPin, 0, 40);
    settings.number("ledPin", "LED Pin", config.ledPin, 0, 40);
    settings.number("oledSda", "OLED SDA", config.oledSda, 0, 40);
    settings.number("oledScl", "OLED SCL", config.oledScl, 0, 40);
    settings.checkbox("oledEnabled", "Enable OLED", config.oledEnabled);
    settings.end();
    
    settings.onSave([]() {
        saveConfigToFile();
        delay(1000);
        ESP.restart();
    });
}

void connectToWifi();
void startApMode();
bool printerConnect();
bool printerPrintTicket(int number, const char* type, const char* queueName, const char* hash);
int getTicketFromServer();
bool confirmPrintOnServer();
void updateTime();

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=== SmartQueue Terminal ===");
    Serial.println("Version: " + String(VERSION));
    
    getMacAddress();
    Serial.print("MAC: ");
    for (int i = 0; i < 6; i++) {
        if (macAddress[i] < 0x10) Serial.print("0");
        Serial.print(macAddress[i], HEX);
        if (i < 5) Serial.print(":");
    }
    Serial.println();
    
    // Load config from FileData
    loadConfigFromFile();
    
    // Generate hash if empty
    if (strlen(config.terminalHash) == 0) {
        mString hash = generateHashFromMac();
        strncpy(config.terminalHash, hash.c_str(), sizeof(config.terminalHash) - 1);
        saveConfigToFile();
    }
    
    pinMode(config.ledPin, OUTPUT);
    pinMode(config.btnPin, INPUT_PULLUP);
    btn.setType(TYPE_HIGH);
    oledInit();
    
    // Setup Settings web interface
    setupSettings();
    
    connectToWifi();
    
    // Start NTP sync if connected
    if (WiFi.status() == WL_CONNECTED) {
        ntp.begin();
        updateTime();
    }
    
    server.begin();
    Serial.println("HTTP server started");
    printerConnect();
    ledBlink(3, 200);
    oledShowStatus("Ready");
}

void loop() {
    server.handleClient();
    settings.tick();  // Handle Settings web interface
    
    // Sync NTP every hour
    if (millis() - lastNtpSync > 3600000UL) {
        if (ntp.tick()) {
            updateTime();
            lastNtpSync = millis();
        }
    }
    
    btn.tick();
    if (btn.isClick()) {
        if (ticketPending) {
            oledShowStatus("Confirming...");
            if (confirmPrintOnServer()) {
                oledShowStatus("Confirmed!");
                ledBlink(2, 100);
                ticketPending = false;
                pendingTicketNumber = 0;
                pendingTicketId = 0;
            } else {
                oledShowStatus("Confirm failed");
                ledBlink(3, 300);
            }
        } else {
            oledShowStatus("Getting ticket...");
            int result = getTicketFromServer();
            if (result > 0) {
                ticketPending = true;
                oledShowTicket(pendingTicketNumber, "T");
                ledBlink(5, 100);
                if (printerPrintTicket(pendingTicketNumber, "T", config.queueLink, config.terminalHash))
                    oledShowStatus("Printed - Press to confirm");
            } else {
                oledShowStatus("Get ticket failed");
                ledBlink(2, 300);
            }
        }
    }
    static unsigned long lastTick = 0;
    if (millis() - lastTick > 1000) {
        lastTick = millis();
        static int timeoutCounter = 0;
        if (ticketPending) {
            timeoutCounter++;
            if (timeoutCounter > 60) {
                ticketPending = false;
                pendingTicketNumber = 0;
                pendingTicketId = 0;
                timeoutCounter = 0;
                oledShowStatus("Timeout - Ready");
            }
        } else timeoutCounter = 0;
    }
    delay(10);
}

// Implementations
void connectToWifi() {
    if (config.ssid.length() == 0) { startApMode(); return; }
    WiFi.mode(WIFI_STA);
    WiFi.begin(config.ssid.c_str(), config.password.c_str());
    oledShowStatus("Connecting WiFi...");
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500); Serial.print("."); attempts++; ledBlink(1, 100);
    }
    if (WiFi.status() == WL_CONNECTED) {
        oledShowStatus("WiFi Connected");
        Serial.println("\nWiFi connected");
        Serial.print("IP: "); Serial.println(WiFi.localIP());
        digitalWrite(config.ledPin, HIGH);
    } else { oledShowStatus("WiFi Failed - AP Mode"); startApMode(); }
}

void startApMode() {
    isApMode = true;
    mString apName = "SmartQueue-";
    apName += String(macAddress[4], HEX); apName += String(macAddress[5], HEX);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apName.c_str(), "smartqueue");
    Serial.println("AP Mode started");
    Serial.print("AP Name: "); Serial.println(apName);
    Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());
    oledShowStatus("AP Mode Active");
}

bool printerConnect() {
    // For ESP32-WROOM-32, using HardwareSerial for thermal printer via UART
    Serial2.begin(9600, SERIAL_8N1, PRINTER_RX, PRINTER_TX);
    Serial.println("UART Printer initialized on Serial2");
    return true;
}

bool printerPrintTicket(int number, const char* type, const char* queueName, const char* hash) {
    mString receipt = "";
    receipt += "\x1B\x40"; receipt += "\x1B\x61\x01";
    receipt += "SMARTQUEUE\n----------------\nQueue: "; receipt += queueName; receipt += "\n";
    receipt += "\x1B\x21\x30"; receipt += "TALON: "; receipt += type; receipt += String(number); receipt += "\n";
    receipt += "\x1B\x21\x00"; receipt += "Date: "; receipt += __DATE__; receipt += " "; receipt += __TIME__; receipt += "\n";
    receipt += "Hash: "; receipt += hash; receipt += "\n----------------\n\x1B\x64\x03";
    Serial2.print(receipt.c_str()); delay(1000); return true;
}

int getTicketFromServer() {
    if (config.serverUrl.length() == 0 || config.queueLink.length() == 0) return -1;
    HTTPClient http;
    mString url = config.serverUrl + "/queues/api/terminal/ticket/";
    http.begin(url.c_str()); http.addHeader("Content-Type", "application/json");
    mString jsonBody = "{\"link\":\""; jsonBody += config.queueLink; jsonBody += "\",\"terminal_hash\":\""; jsonBody += config.terminalHash; jsonBody += "\"}";
    int httpCode = http.POST(jsonBody.c_str());
    if (httpCode > 0) {
        mString response = http.getString(); Serial.println("Response: " + response);
        int ticketNumPos = response.indexOf("\"ticket_number\":");
        if (ticketNumPos > 0) { int start = ticketNumPos + 16; int end = response.indexOf(',', start); if (end == -1) end = response.indexOf('}', start); mString numStr = response.substring(start, end); pendingTicketNumber = numStr.toInt(); }
        int ticketIdPos = response.indexOf("\"ticket_id\":");
        if (ticketIdPos > 0) { int start = ticketIdPos + 12; int end = response.indexOf(',', start); if (end == -1) end = response.indexOf('}', start); mString idStr = response.substring(start, end); pendingTicketId = idStr.toInt(); }
        http.end(); if (pendingTicketNumber > 0) return 1;
    }
    http.end(); return -1;
}

bool confirmPrintOnServer() {
    if (pendingTicketId <= 0) return false;
    HTTPClient http;
    mString url = config.serverUrl + "/queues/api/terminal/confirm/";
    http.begin(url.c_str()); http.addHeader("Content-Type", "application/json");
    mString jsonBody = "{\"ticket_id\":"; jsonBody += String(pendingTicketId); jsonBody += ",\"terminal_hash\":\""; jsonBody += config.terminalHash; jsonBody += "\"}";
    int httpCode = http.POST(jsonBody.c_str());
    if (httpCode > 0) { mString response = http.getString(); Serial.println("Confirm response: " + response); http.end(); return response.indexOf("\"success\":true") > 0; }
    http.end(); return false;
}

void updateTime() {
    if (ntp.valid()) {
        snprintf(currentTime, sizeof(currentTime), "%02d:%02d:%02d %02d.%02d.%04d",
            ntp.hour(), ntp.minute(), ntp.second(),
            ntp.day(), ntp.month(), ntp.year());
        Serial.printf("NTP Time: %s\n", currentTime);
    } else {
        strcpy(currentTime, "No NTP sync");
    }
}
