/*
 * SmartQueue Terminal - ESP32-C3 SuperMicro
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
#include <BluetoothSerial.h>

#define VERSION "1.0.0"
#define DEFAULT_BTN_PIN 9
#define DEFAULT_LED_PIN 8
#define DEFAULT_OLED_SDA 6
#define DEFAULT_OLED_SCL 7

GyverOLED<SSD1306_128x64, OLED_NO_BUFFER> oled;
GyverButton btn(DEFAULT_BTN_PIN);
BluetoothSerial BT;
WebServer server(80);

struct Config {
    mString ssid = "";
    mString password = "";
    mString serverUrl = "http://192.168.1.100:8000";
    mString terminalHash = "";
    mString queueLink = "";
    mString webPassword = "admin";
    uint8_t btnPin = DEFAULT_BTN_PIN;
    uint8_t ledPin = DEFAULT_LED_PIN;
    uint8_t oledSda = DEFAULT_OLED_SDA;
    uint8_t oledScl = DEFAULT_OLED_SCL;
    bool oledEnabled = true;
    bool configured = false;
} config;

uint8_t macAddress[6];
bool isApMode = false;
bool ticketPending = false;
int pendingTicketNumber = 0;
int pendingTicketId = 0;

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

void connectToWifi();
void startApMode();
bool printerConnect();
bool printerPrintTicket(int number, const char* type, const char* queueName, const char* hash);
int getTicketFromServer();
bool confirmPrintOnServer();
String sendHeader();
void handleRoot();
void handleSave();
void handleUpdate();
void handleNotFound();
bool loadConfig();
void saveConfig();

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
    if (!loadConfig() || !config.configured) {
        Serial.println("First run or config missing - starting AP mode");
        config.terminalHash = generateHashFromMac();
        saveConfig();
    }
    pinMode(config.ledPin, OUTPUT);
    pinMode(config.btnPin, INPUT_PULLUP);
    btn.setType(TYPE_HIGH);
    oledInit();
    connectToWifi();
    server.on("/", handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.on("/update", HTTP_POST, [](){ server.send(200, "text/plain", "Update complete. Rebooting..."); delay(1000); ESP.restart(); }, handleUpdate);
    server.onNotFound(handleNotFound);
    server.begin();
    Serial.println("HTTP server started");
    printerConnect();
    ledBlink(3, 200);
    oledShowStatus("Ready");
}

void loop() {
    server.handleClient();
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
                if (BT.connected()) {
                    mString hash = config.terminalHash;
                    mString queueName = config.queueLink;
                    if (printerPrintTicket(pendingTicketNumber, "T", queueName.c_str(), hash.c_str()))
                        oledShowStatus("Printed - Press to confirm");
                }
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
    if (!BT.begin("SmartQueuePrinter")) { Serial.println("BT init failed"); return false; }
    Serial.println("BT initialized"); return true;
}

bool printerPrintTicket(int number, const char* type, const char* queueName, const char* hash) {
    if (!BT.connected()) { Serial.println("Printer not connected"); return false; }
    mString receipt = "";
    receipt += "\x1B\x40"; receipt += "\x1B\x61\x01";
    receipt += "SMARTQUEUE\n----------------\nQueue: "; receipt += queueName; receipt += "\n";
    receipt += "\x1B\x21\x30"; receipt += "TALON: "; receipt += type; receipt += String(number); receipt += "\n";
    receipt += "\x1B\x21\x00"; receipt += "Date: "; receipt += __DATE__; receipt += " "; receipt += __TIME__; receipt += "\n";
    receipt += "Hash: "; receipt += hash; receipt += "\n----------------\n\x1B\x64\x03";
    BT.print(receipt.c_str()); delay(1000); return true;
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

String sendHeader() {
    String header = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'><title>SmartQueue Terminal</title><style>";
    header += "body{font-family:Arial;margin:20px;background:#f5f5f5;}.card{background:white;padding:20px;margin:10px 0;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1);}";
    header += "input,select{width:100%;padding:10px;margin:5px 0;border:1px solid #ddd;border-radius:4px;box-sizing:border-box;}button{background:#007bff;color:white;padding:10px 20px;border:none;border-radius:4px;cursor:pointer;}button:hover{background:#0056b3;}";
    header += ".status{padding:10px;background:#e7f3ff;border-radius:4px;margin:10px 0;}.preview{background:#f8f9fa;padding:15px;border:1px dashed #ccc;font-family:monospace;white-space:pre-wrap;}</style></head><body>";
    return header;
}

void handleRoot() {
    String html = sendHeader();
    html += "<div class='card'><h2>SmartQueue Terminal</h2><div class='status'><strong>Status:</strong> ";
    if (isApMode) html += "<span style='color:orange'>AP Mode</span>";
    else if (WiFi.status() == WL_CONNECTED) html += "<span style='color:green'>Connected</span> - IP: " + WiFi.localIP().toString();
    else html += "<span style='color:red'>Disconnected</span>";
    html += "<br><strong>MAC:</strong> ";
    for (int i = 0; i < 6; i++) { if (macAddress[i] < 0x10) html += "0"; html += String(macAddress[i], HEX); if (i < 5) html += ":"; }
    html += "<br><strong>Hash:</strong> " + String(config.terminalHash) + "<br><strong>Firmware:</strong> " + String(VERSION) + "</div></div>";
    html += "<div class='card'><h3>Configuration</h3><form method='POST' action='/save'>";
    html += "<label>WiFi SSID:</label><input type='text' name='ssid' value='" + String(config.ssid.c_str()) + "'>";
    html += "<label>WiFi Password:</label><input type='password' name='password' value='" + String(config.password.c_str()) + "'>";
    html += "<label>Server URL:</label><input type='text' name='serverUrl' value='" + String(config.serverUrl.c_str()) + "'>";
    html += "<label>Queue Link:</label><input type='text' name='queueLink' value='" + String(config.queueLink.c_str()) + "'>";
    html += "<label>Web Password:</label><input type='password' name='webPassword' value='" + String(config.webPassword.c_str()) + "'>";
    html += "<label>Button Pin:</label><input type='number' name='btnPin' value='" + String(config.btnPin) + "'>";
    html += "<label>LED Pin:</label><input type='number' name='ledPin' value='" + String(config.ledPin) + "'>";
    html += "<label>OLED SDA:</label><input type='number' name='oledSda' value='" + String(config.oledSda) + "'>";
    html += "<label>OLED SCL:</label><input type='number' name='oledScl' value='" + String(config.oledScl) + "'>";
    html += "<label><input type='checkbox' name='oledEnabled' " + String(config.oledEnabled ? "checked" : "") + "> Enable OLED</label>";
    html += "<button type='submit'>Save & Reboot</button></form></div>";
    html += "<div class='card'><h3>Ticket Preview</h3><div class='preview'>SMARTQUEUE\\n----------------\\nQueue: " + String(config.queueLink.c_str()) + "\\nTALON: T[NUMBER]\\nDate: " + String(__DATE__) + " " + String(__TIME__) + "\\nHash: [HASH]\\n----------------\\n[QR CODE]\\n</div></div>";
    html += "<div class='card'><h3>OTA Update</h3><form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='firmware' accept='.bin'><button type='submit'>Upload & Update</button></form></div></body></html>";
    server.send(200, "text/html", html);
}

void handleSave() {
    if (server.hasArg("ssid")) config.ssid = server.arg("ssid").c_str();
    if (server.hasArg("password")) config.password = server.arg("password").c_str();
    if (server.hasArg("serverUrl")) config.serverUrl = server.arg("serverUrl").c_str();
    if (server.hasArg("queueLink")) config.queueLink = server.arg("queueLink").c_str();
    if (server.hasArg("webPassword")) config.webPassword = server.arg("webPassword").c_str();
    if (server.hasArg("btnPin")) config.btnPin = server.arg("btnPin").toInt();
    if (server.hasArg("ledPin")) config.ledPin = server.arg("ledPin").toInt();
    if (server.hasArg("oledSda")) config.oledSda = server.arg("oledSda").toInt();
    if (server.hasArg("oledScl")) config.oledScl = server.arg("oledScl").toInt();
    config.oledEnabled = server.hasArg("oledEnabled"); config.configured = true;
    saveConfig();
    String html = sendHeader() + "<div class='card'><h3>Saved!</h3><p>Configuration saved. Rebooting...</p><script>setTimeout(function(){window.location.href='/';},2000);</script></div></body></html>";
    server.send(200, "text/html", html); delay(1000); ESP.restart();
}

void handleUpdate() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) { Serial.printf("Update: %s\n", upload.filename.c_str()); if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial); }
    else if (upload.status == UPLOAD_FILE_WRITE) { if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) Update.printError(Serial); }
    else if (upload.status == UPLOAD_FILE_END) { if (Update.end(true)) Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize); else Update.printError(Serial); }
}

void handleNotFound() { server.send(404, "text/plain", "Not Found"); }

bool loadConfig() {
    if (!LittleFS.begin()) { Serial.println("LittleFS mount failed"); return false; }
    File file = LittleFS.open("/config.txt", "r"); if (!file) { Serial.println("Config file not found"); return false; }
    mString line;
    while (file.available()) {
        line = file.readStringUntil('\n').c_str(); int eqPos = line.indexOf('=');
        if (eqPos > 0) {
            mString key = line.substring(0, eqPos); mString value = line.substring(eqPos + 1);
            if (key == "ssid") config.ssid = value; else if (key == "password") config.password = value;
            else if (key == "serverUrl") config.serverUrl = value; else if (key == "queueLink") config.queueLink = value;
            else if (key == "webPassword") config.webPassword = value; else if (key == "btnPin") config.btnPin = value.toInt();
            else if (key == "ledPin") config.ledPin = value.toInt(); else if (key == "oledSda") config.oledSda = value.toInt();
            else if (key == "oledScl") config.oledScl = value.toInt(); else if (key == "oledEnabled") config.oledEnabled = value == "1";
            else if (key == "terminalHash") config.terminalHash = value; else if (key == "configured") config.configured = value == "1";
        }
    }
    file.close(); return true;
}

void saveConfig() {
    File file = LittleFS.open("/config.txt", "w"); if (!file) { Serial.println("Failed to open config for writing"); return; }
    file.println("ssid=" + String(config.ssid.c_str())); file.println("password=" + String(config.password.c_str()));
    file.println("serverUrl=" + String(config.serverUrl.c_str())); file.println("queueLink=" + String(config.queueLink.c_str()));
    file.println("webPassword=" + String(config.webPassword.c_str())); file.println("btnPin=" + String(config.btnPin));
    file.println("ledPin=" + String(config.ledPin)); file.println("oledSda=" + String(config.oledSda));
    file.println("oledScl=" + String(config.oledScl)); file.println("oledEnabled=" + String(config.oledEnabled ? "1" : "0"));
    file.println("terminalHash=" + String(config.terminalHash.c_str())); file.println("configured=" + String(config.configured ? "1" : "0"));
    file.close(); Serial.println("Config saved");
}
