#include "bt_printer.h"
#include "raster_font.h"
#include "logger.h"
#include <NimBLEDevice.h>
#include <stdlib.h>

#define TAG "BT"

// ─── BLE объекты ─────────────────────────────────────────────────────────────
static NimBLEClient*                pClient    = nullptr;
static NimBLERemoteCharacteristic*  pWriteChar = nullptr;
static bool bleStarted = false;

// Найденный UUID характеристики (для логирования)
static char foundCharUUID[48] = {0};

// Уменьшенный размер чанка для стабильности
#ifndef BLE_CHUNK_SIZE
#define BLE_CHUNK_SIZE 20
#endif
#ifndef BLE_CHUNK_DELAY_MS
#define BLE_CHUNK_DELAY_MS 15
#endif
#ifndef BLE_LINE_DELAY_MS
#define BLE_LINE_DELAY_MS 20
#endif

// =============================================================================
//  Список кандидатов для записи (в порядке приоритета)
//  Добавляем все известные UUID принтеров этого класса
// =============================================================================
static const char* WRITE_CANDIDATES[] = {
    "0000ffe1-0000-1000-8000-00805f9b34fb",   // FFE1 — этот принтер!
    "0000ae01-0000-1000-8000-00805f9b34fb",   // AE01 — другие FunnyPrint
    "5833ff02-9b8b-5191-6142-22a4536ef123",   // 5833FF02 — этот принтер (alt)
    "6e400002-b5a3-f393-e0a9-e50e24dcca9e",   // NUS RX — Nordic UART
    nullptr
};

// =============================================================================
class PrinterCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* cli) {
        LOGI(TAG, "BLE onConnect");
    }
    void onDisconnect(NimBLEClient* cli, int reason) {
        g_printerConnected = false;
        LOGW(TAG, "BLE onDisconnect (reason=%d)", reason);
    }
};

static PrinterCallbacks bleCallbacks;

// =============================================================================
bool bt_is_alive() {
    if (!bleStarted) return false;
    return (pClient != nullptr &&
            pClient->isConnected() &&
            pWriteChar != nullptr);
}

// =============================================================================
bool bt_write(const uint8_t* d, size_t n) {
    if (!d || n == 0) return false;
    if (!g_printerConnected || !bt_is_alive()) {
        g_printerConnected = false;
        return false;
    }

    // Для характеристики 0xffe1 используем write without response (true)
    bool useNoResponse = pWriteChar->canWriteNoResponse();

    for (size_t off = 0; off < n; off += BLE_CHUNK_SIZE) {
        size_t chunk = min((size_t)BLE_CHUNK_SIZE, n - off);
        // Пробуем записать с повторами при ошибке
        bool ok = false;
        for (int retry = 0; retry < 3 && !ok; retry++) {
            ok = pWriteChar->writeValue(d + off, chunk, useNoResponse);
            if (!ok) {
                LOGW(TAG, "Write retry %d at %u", retry + 1, off);
                delay(10);
            }
        }
        if (!ok) {
            LOGE(TAG, "Write failed at %u after retries", off);
            return false;
        }
        delay(BLE_CHUNK_DELAY_MS);
    }
    return true;
}

// =============================================================================
static void readMac(char* buf, size_t sz) {
    auto v = g_db[K_BT_MAC];
    const char* s = v.toString().c_str();
    if (strlen(s) < 17) {
        strncpy(buf, DEFAULT_PRINTER_MAC, sz - 1);
        LOGW(TAG, "MAC default: %s", DEFAULT_PRINTER_MAC);
    } else {
        strncpy(buf, s, sz - 1);
    }
    buf[sz - 1] = '\0';
}

// =============================================================================
//  Поиск характеристики записи среди всех сервисов
//  Пробуем все UUID из WRITE_CANDIDATES, затем первую с WRITE-свойством
// =============================================================================
static NimBLERemoteCharacteristic* findWriteChar(
    const std::vector<NimBLERemoteService*>& svcList)
{
    // ── 1. Ищем по известным UUID ──
    for (int ci = 0; WRITE_CANDIDATES[ci] != nullptr; ci++) {
        NimBLEUUID uuid(WRITE_CANDIDATES[ci]);
        for (auto* svc : svcList) {
            auto* ch = svc->getCharacteristic(uuid);
            if (ch) {
                snprintf(foundCharUUID, sizeof(foundCharUUID), "%s",
                         ch->getUUID().toString().c_str());
                LOGI(TAG, "Found candidate [%d]: %s in SVC %s",
                     ci, foundCharUUID,
                     svc->getUUID().toString().c_str());
                return ch;
            }
        }
    }

    // ── 2. Fallback: первая writable в не-стандартном сервисе ──
    LOGW(TAG, "No known UUID, scanning all writable chars...");
    for (auto* svc : svcList) {
        // NimBLE 2.x: фильтруем стандартные GATT по строке UUID
        // Стандартные: "0x1800", "0x1801", "0x180a" и т.д.
        const char* svcStr = svc->getUUID().toString().c_str();
        if (strncmp(svcStr, "0x18", 4) == 0 ||
            strncmp(svcStr, "0x00", 4) == 0) {
            LOGD(TAG, "Skip GATT SVC: %s", svcStr);
            continue;
            }

        std::vector<NimBLERemoteCharacteristic*> charList =
            svc->getCharacteristics(true);

        for (auto* ch : charList) {
            if (ch->canWrite() || ch->canWriteNoResponse()) {
                snprintf(foundCharUUID, sizeof(foundCharUUID), "%s",
                         ch->getUUID().toString().c_str());
                LOGI(TAG, "Fallback: %s in SVC %s",
                     foundCharUUID, svcStr);
                return ch;
            }
        }
    }

    return nullptr;
}

// =============================================================================
bool bt_connect() {
    char macBuf[20];
    readMac(macBuf, sizeof(macBuf));

    if (strcmp(macBuf, DEFAULT_PRINTER_MAC) == 0) {
        LOGW(TAG, "MAC is default, skip");
        return false;
    }

    LOGI(TAG, "Connecting to %s ...", macBuf);
    LOGI(TAG, "Heap: %u", ESP.getFreeHeap());

    if (!bleStarted) {
        LOGI(TAG, "Starting NimBLE...");
        NimBLEDevice::init(BT_NAME);
        NimBLEDevice::setPower(ESP_PWR_LVL_P9);
        bleStarted = true;
        delay(500);
        LOGI(TAG, "NimBLE OK. Heap: %u", ESP.getFreeHeap());
    }

    // Очистка
    if (pClient) {
        if (pClient->isConnected()) {
            pClient->disconnect();
            delay(500);
        }
        NimBLEDevice::deleteClient(pClient);
        pClient = nullptr;
        pWriteChar = nullptr;
        delay(300);
    }

    // ── Подключение ──
    pClient = NimBLEDevice::createClient();
    pClient->setClientCallbacks(&bleCallbacks, false);
    pClient->setConnectTimeout(15);

    LOGI(TAG, "Trying PUBLIC...");
    bool connected = pClient->connect(NimBLEAddress(macBuf, BLE_ADDR_PUBLIC));

    if (!connected) {
        LOGI(TAG, "PUBLIC fail, trying RANDOM...");
        NimBLEDevice::deleteClient(pClient);
        delay(300);
        pClient = NimBLEDevice::createClient();
        pClient->setClientCallbacks(&bleCallbacks, false);
        pClient->setConnectTimeout(15);
        connected = pClient->connect(NimBLEAddress(macBuf, BLE_ADDR_RANDOM));
    }

    if (!connected) {
        LOGE(TAG, "Connection failed! (printer ON? correct MAC?)");
        NimBLEDevice::deleteClient(pClient);
        pClient = nullptr;
        return false;
    }

    LOGI(TAG, "BLE link OK! MTU=%u", pClient->getMTU());
    delay(500);

    // ── Обнаружение сервисов ──
    LOGI(TAG, "Discovering services...");
    std::vector<NimBLERemoteService*> svcList = pClient->getServices(true);

    if (svcList.empty()) {
        LOGE(TAG, "No services!");
        pClient->disconnect();
        NimBLEDevice::deleteClient(pClient);
        pClient = nullptr;
        return false;
    }

    LOGI(TAG, "Services (%d):", svcList.size());
    for (auto* svc : svcList) {
        LOGI(TAG, "  SVC: %s", svc->getUUID().toString().c_str());
        // Получаем характеристики для каждого сервиса
        std::vector<NimBLERemoteCharacteristic*> charList =
            svc->getCharacteristics(true);
        for (auto* ch : charList) {
            LOGI(TAG, "    CHAR: %s  W=%d WNR=%d N=%d",
                 ch->getUUID().toString().c_str(),
                 ch->canWrite() ? 1 : 0,
                 ch->canWriteNoResponse() ? 1 : 0,
                 ch->canNotify() ? 1 : 0);
        }
    }

    // ── Поиск характеристики записи ──
    pWriteChar = findWriteChar(svcList);

    if (!pWriteChar) {
        LOGE(TAG, "No writable characteristic found!");
        pClient->disconnect();
        NimBLEDevice::deleteClient(pClient);
        pClient = nullptr;
        return false;
    }

    LOGI(TAG, "Using char: %s", foundCharUUID);
    LOGI(TAG, "  canWrite=%d canWriteNR=%d",
         pWriteChar->canWrite() ? 1 : 0,
         pWriteChar->canWriteNoResponse() ? 1 : 0);

    // ── Инициализация принтера ──
    LOGI(TAG, "Sending ESC @...");
    const uint8_t initCmd[] = {0x1B, 0x40};
    // ⚠️ ВАЖНО: используем write without response для стабильности
    bool useNoResponse = pWriteChar->canWriteNoResponse();
    bool initOk = false;
    for (int retry = 0; retry < 3 && !initOk; retry++) {
        initOk = pWriteChar->writeValue(initCmd, 2, useNoResponse);
        if (!initOk) {
            LOGW(TAG, "Init retry %d", retry + 1);
            delay(50);
        }
    }
    if (!initOk) {
        LOGW(TAG, "Init send failed after retries");
    }
    delay(300);

    g_printerConnected = true;
    delay(500);

    if (pClient->isConnected()) {
        LOGI(TAG, "Printer READY! Char=%s Heap=%u",
             foundCharUUID, ESP.getFreeHeap());
        return true;
    }

    LOGW(TAG, "Dropped after init");
    g_printerConnected = false;
    pWriteChar = nullptr;
    return false;
}

// =============================================================================
void bt_disconnect() {
    LOGI(TAG, "Disconnecting...");
    if (bleStarted && pClient && pClient->isConnected()) {
        pClient->disconnect();
    }
    pWriteChar = nullptr;
    g_printerConnected = false;
    delay(500);
    LOGI(TAG, "Done");
}

// =============================================================================
//  Отправка строки растра
//  Принтер использует сервис FFE6/FFE1 — протокол аналогичен AE01:
//    заголовок [12 2A 01 01] + 48 байт данных
// =============================================================================
static bool sendRasterLine(const uint8_t* lineData) {
    const uint8_t hdr[] = {0x12, 0x2A, 0x01, 0x01};
    if (!bt_write(hdr, sizeof(hdr))) return false;
    if (!bt_write(lineData, PRINTER_WIDTH_BYTES)) return false;
    delay(BLE_LINE_DELAY_MS);
    return true;
}

// =============================================================================
void raster_init() {
    if (!g_printerConnected) return;
    LOGD(TAG, "ESC @");
    const uint8_t cmd[] = {0x1B, 0x40};
    bt_write(cmd, 2);
    delay(100);
}

void raster_set_concentration(uint8_t level) {
    if (!g_printerConnected) return;
    if (level < 1) level = 1;
    if (level > 6) level = 6;
    LOGD(TAG, "Concentration %d", level);
    const uint8_t cmd[] = {0x1F, 0x11, 0x02, level};
    bt_write(cmd, 4);
    delay(50);
}

void raster_feed(uint8_t lines) {
    if (!g_printerConnected) return;
    LOGD(TAG, "Feed %d", lines);
    const uint8_t cmd[] = {0x1B, 0x64, lines};
    bt_write(cmd, 3);
}

void raster_empty(int heightPx) {
    if (!g_printerConnected || heightPx <= 0) return;
    uint8_t emptyLine[PRINTER_WIDTH_BYTES];
    memset(emptyLine, 0, sizeof(emptyLine));
    for (int y = 0; y < heightPx; y++) {
        sendRasterLine(emptyLine);
    }
}

void raster_separator(char style, uint8_t heightPx) {
    if (!g_printerConnected) return;
    uint8_t line[PRINTER_WIDTH_BYTES];
    for (int y = 0; y < heightPx; y++) {
        memset(line, 0, sizeof(line));
        for (int x = 0; x < PRINTER_WIDTH_PX; x++) {
            bool on = false;
            switch (style) {
                case '-': on = ((x / 4) % 2 == 0); break;
                case '=': on = (y == 0 || y == heightPx - 1); break;
                default:  on = true; break;
            }
            if (on) line[x >> 3] |= (0x80 >> (x & 7));
        }
        sendRasterLine(line);
    }
}

void raster_print_line(const char* text, uint8_t scale,
                       uint8_t align, bool bold) {
    if (!g_printerConnected || !text) return;

    int textLen = (int)strlen(text);
    if (textLen == 0) { raster_empty(FONT_CHAR_H * scale); return; }

    const int charW    = FONT_CHAR_W * scale;
    const int charH    = FONT_CHAR_H * scale;
    const int maxChars = PRINTER_WIDTH_PX / charW;
    if (textLen > maxChars) textLen = maxChars;

    int textPx  = textLen * charW;
    int offsetX = 0;
    if (align == 1) offsetX = (PRINTER_WIDTH_PX - textPx) / 2;
    if (align == 2) offsetX = PRINTER_WIDTH_PX - textPx;
    if (offsetX < 0) offsetX = 0;

    uint8_t line[PRINTER_WIDTH_BYTES];

    for (int py = 0; py < charH; py++) {
        memset(line, 0, sizeof(line));
        int fontRow = py / scale;

        for (int ci = 0; ci < textLen; ci++) {
            const uint8_t* glyph = fontGlyph(text[ci]);
            uint8_t fb = pgm_read_byte(&glyph[fontRow]);
            if (bold) fb |= (fb >> 1);

            for (int col = 0; col < FONT_CHAR_W; col++) {
                if (!((fb >> (7 - col)) & 1)) continue;
                for (int sx = 0; sx < scale; sx++) {
                    int px = offsetX + ci * charW + col * scale + sx;
                    if (px >= 0 && px < PRINTER_WIDTH_PX) {
                        line[px >> 3] |= (0x80 >> (px & 7));
                    }
                }
            }
        }
        sendRasterLine(line);
    }
}

// =============================================================================
void printTicket(const char* num, const char* queue, const char* eta) {
    if (!g_printerConnected || !bt_is_alive()) {
        LOGE("PRN", "No connection");
        g_printerConnected = false;
        return;
    }

    LOGI("PRN", "=== TICKET %s ===", num);

    raster_init();
    delay(200);
    raster_set_concentration(4);

    raster_print_line("SMART QUEUE", 2, 1, true);
    raster_empty(4);
    raster_separator('-', 2);
    raster_empty(4);
    raster_print_line(queue, 1, 1, false);
    raster_empty(4);
    raster_print_line(num, 2, 1, true);
    raster_empty(4);

    if (eta && eta[0]) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Wait: %s", eta);
        raster_print_line(buf, 1, 0, false);
        raster_empty(4);
    }

    raster_separator('-', 2);
    raster_feed(4);

    LOGI("PRN", "=== DONE ===");
}