#include "bt_printer.h"
#include "raster_font.h"
#include "logger.h"
#include <stdlib.h>

#define TAG "BT"

#if BT_CLASSIC
    #include <BluetoothSerial.h>
    extern BluetoothSerial SerialBT;
#else
    #include <NimBLEDevice.h>
    extern NimBLEClient*                pBleClient;
    extern NimBLERemoteCharacteristic*  pBleRxChar;
#endif

// =============================================================================
//  Проверка живости соединения
// =============================================================================
bool bt_is_alive() {
#if BT_CLASSIC
    return SerialBT.connected();
#else
    return (pBleClient != nullptr &&
            pBleClient->isConnected() &&
            pBleRxChar != nullptr);
#endif
}

// =============================================================================
//  Отправка данных
// =============================================================================
bool bt_write(const uint8_t* d, size_t n) {
    if (!g_printerConnected || !d || n == 0) {
        LOGW(TAG, "write: not connected or empty data (n=%u)", n);
        return false;
    }

    if (!bt_is_alive()) {
        LOGE(TAG, "write: connection lost!");
        g_printerConnected = false;
        return false;
    }

#if BT_CLASSIC
    for (size_t off = 0; off < n; off += SPP_CHUNK_SIZE) {
        size_t chunk = min((size_t)SPP_CHUNK_SIZE, n - off);
        size_t written = SerialBT.write(d + off, chunk);
        if (written != chunk) {
            LOGE(TAG, "SPP write error: sent %u/%u at offset %u",
                 written, chunk, off);
            return false;
        }
        if (n - off > SPP_CHUNK_SIZE) delay(SPP_CHUNK_DELAY_MS);
    }
    LOGD(TAG, "SPP wrote %u bytes OK", n);
    return true;
#else
    for (size_t off = 0; off < n; off += BLE_CHUNK_SIZE) {
        size_t chunk = min((size_t)BLE_CHUNK_SIZE, n - off);
        if (!pBleRxChar->writeValue(d + off, chunk, true)) {
            LOGE(TAG, "BLE write error at offset %u", off);
            return false;
        }
        delay(BLE_CHUNK_DELAY_MS);
    }
    LOGD(TAG, "BLE wrote %u bytes OK", n);
    return true;
#endif
}

// =============================================================================
//  Отправка растрового блока
// =============================================================================
static bool sendRasterBlock(const uint8_t* data, int widthBytes, int height) {
    LOGD(TAG, "Raster block: %dx%d (%d bytes)",
         widthBytes * 8, height, widthBytes * height);

    for (int y = 0; y < height; y += RASTER_BLOCK_HEIGHT) {
        int blockH = min(RASTER_BLOCK_HEIGHT, height - y);

        uint8_t hdr[] = {
            0x1D, 0x76, 0x30, 0x00,
            (uint8_t)(widthBytes & 0xFF),
            (uint8_t)((widthBytes >> 8) & 0xFF),
            (uint8_t)(blockH & 0xFF),
            (uint8_t)((blockH >> 8) & 0xFF)
        };

        LOGD(TAG, "Block y=%d h=%d hdr=[%02X %02X %02X %02X %02X %02X %02X %02X]",
             y, blockH,
             hdr[0],hdr[1],hdr[2],hdr[3],hdr[4],hdr[5],hdr[6],hdr[7]);

        if (!bt_write(hdr, sizeof(hdr))) {
            LOGE(TAG, "Failed to send raster header");
            return false;
        }
        if (!bt_write(data + y * widthBytes, blockH * widthBytes)) {
            LOGE(TAG, "Failed to send raster data block y=%d", y);
            return false;
        }

        // Даём принтеру обработать
        int waitMs = blockH * 2 + 20;
        LOGD(TAG, "Wait %d ms for printer", waitMs);
        delay(waitMs);
    }
    return true;
}

// =============================================================================
//  Подключение с retry и подробным логированием
// =============================================================================

// Вспомогательная: читаем MAC из БД
static void readMac(char* macBuf, size_t sz) {
    auto v = g_db[K_BT_MAC];
    const char* s = v.toString().c_str();
    if (strlen(s) < 17) {
        strncpy(macBuf, DEFAULT_PRINTER_MAC, sz - 1);
        LOGW(TAG, "MAC not set, using default %s", DEFAULT_PRINTER_MAC);
    } else {
        strncpy(macBuf, s, sz - 1);
    }
    macBuf[sz - 1] = '\0';
}

#if BT_CLASSIC
// ─── Classic SPP ─────────────────────────────────────────────────────────────

bool bt_connect() {
    char macBuf[20];
    readMac(macBuf, sizeof(macBuf));

    uint8_t addr[6];
    if (sscanf(macBuf, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
               &addr[0],&addr[1],&addr[2],&addr[3],&addr[4],&addr[5]) != 6) {
        LOGE(TAG, "Invalid MAC format: %s", macBuf);
        return false;
    }

    LOGI(TAG, "Connecting SPP to %s ...", macBuf);

    for (int attempt = 1; attempt <= BT_CONNECT_RETRIES; attempt++) {
        LOGI(TAG, "Attempt %d/%d", attempt, BT_CONNECT_RETRIES);

        SerialBT.end();
        delay(300);

        if (!SerialBT.begin(BT_NAME_CLASSIC, true)) {
            LOGE(TAG, "SerialBT.begin() failed");
            delay(BT_RETRY_DELAY_MS);
            continue;
        }
        LOGD(TAG, "SerialBT initialized as master");

        bool ok = SerialBT.connect(addr);
        if (ok && SerialBT.connected()) {
            g_printerConnected = true;
            LOGI(TAG, "SPP connected on attempt %d!", attempt);

            // Пауза для инициализации принтера
            delay(500);
            raster_init();
            delay(200);

            // Проверяем что соединение живое после init
            if (bt_is_alive()) {
                LOGI(TAG, "Printer ready");
                return true;
            } else {
                LOGW(TAG, "Connection dropped after init");
                g_printerConnected = false;
            }
        } else {
            LOGW(TAG, "SPP connect failed (attempt %d)", attempt);
        }

        delay(BT_RETRY_DELAY_MS);
    }

    LOGE(TAG, "All %d SPP attempts failed", BT_CONNECT_RETRIES);
    g_printerConnected = false;
    return false;
}

void bt_disconnect() {
    LOGI(TAG, "Disconnecting SPP...");
    SerialBT.disconnect();
    g_printerConnected = false;
    delay(300);
    LOGI(TAG, "Disconnected");
}

#else
// ─── BLE NUS ─────────────────────────────────────────────────────────────────

class BtPrinterCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* cli) override {
        LOGI("BLE", "onConnect callback");
    }
    void onDisconnect(NimBLEClient* cli) override {
        g_printerConnected = false;
        LOGW("BLE", "onDisconnect callback (reason may be timeout or manual)");
    }
};

static BtPrinterCallbacks bleCallbacks;

bool bt_connect() {
    char macBuf[20];
    readMac(macBuf, sizeof(macBuf));

    LOGI(TAG, "Connecting BLE to %s ...", macBuf);

    for (int attempt = 1; attempt <= BT_CONNECT_RETRIES; attempt++) {
        LOGI(TAG, "Attempt %d/%d", attempt, BT_CONNECT_RETRIES);

        // Очистка предыдущего клиента
        if (pBleClient) {
            if (pBleClient->isConnected()) {
                pBleClient->disconnect();
                delay(200);
            }
            NimBLEDevice::deleteClient(pBleClient);
            pBleClient = nullptr;
            pBleRxChar = nullptr;
        }

        // (Re)init NimBLE
        if (!NimBLEDevice::getInitialized()) {
            NimBLEDevice::init(BT_NAME_BLE);
            NimBLEDevice::setPower(ESP_PWR_LVL_P9);
            LOGD(TAG, "NimBLE initialized");
        }

        pBleClient = NimBLEDevice::createClient();
        pBleClient->setClientCallbacks(&bleCallbacks, false);
        pBleClient->setConnectTimeout(10);  // 10 секунд таймаут

        LOGD(TAG, "Trying PUBLIC address...");
        bool connected = pBleClient->connect(
            NimBLEAddress(macBuf, BLE_ADDR_PUBLIC));

        if (!connected) {
            LOGD(TAG, "PUBLIC failed, trying RANDOM...");
            connected = pBleClient->connect(
                NimBLEAddress(macBuf, BLE_ADDR_RANDOM));
        }

        if (!connected) {
            LOGW(TAG, "BLE connect failed (attempt %d)", attempt);
            delay(BT_RETRY_DELAY_MS);
            continue;
        }

        LOGI(TAG, "BLE link established");

        // MTU negotiation
        uint16_t mtu = pBleClient->getMTU();
        LOGD(TAG, "Default MTU: %u", mtu);
        // Не запрашиваем слишком большой MTU — может упасть
        // pBleClient->setMTU(247);  // опционально
        // mtu = pBleClient->getMTU();
        // LOGD(TAG, "Negotiated MTU: %u", mtu);

        // Поиск NUS сервиса
        LOGD(TAG, "Looking for NUS service %s ...", NUS_SVC_UUID);
        auto* svc = pBleClient->getService(NUS_SVC_UUID);
        if (!svc) {
            LOGE(TAG, "NUS service NOT FOUND");

            // Перечисляем все сервисы для отладки
            auto* svcs = pBleClient->getServices(true);
            if (svcs) {
                LOGD(TAG, "Available services (%d):", svcs->size());
                for (auto& s : *svcs) {
                    LOGD(TAG, "  SVC: %s", s->getUUID().toString().c_str());
                }
            }

            pBleClient->disconnect();
            delay(BT_RETRY_DELAY_MS);
            continue;
        }
        LOGI(TAG, "NUS service found");

        // Поиск RX характеристики
        LOGD(TAG, "Looking for RX char %s ...", NUS_RX_UUID);
        pBleRxChar = svc->getCharacteristic(NUS_RX_UUID);
        if (!pBleRxChar) {
            LOGE(TAG, "RX characteristic NOT FOUND");

            // Перечисляем характеристики
            auto chars = svc->getCharacteristics(true);
            if (chars) {
                for (auto& c : *chars) {
                    LOGD(TAG, "  CHAR: %s props=0x%02X",
                         c->getUUID().toString().c_str(),
                         c->getProperties());
                }
            }

            pBleClient->disconnect();
            delay(BT_RETRY_DELAY_MS);
            continue;
        }

        // Проверяем что характеристика поддерживает запись
        uint32_t props = pBleRxChar->getProperties();
        LOGD(TAG, "RX char found, props=0x%02X (Write=%d WriteNR=%d)",
             props,
             (props & NIMBLE_PROPERTY::WRITE) ? 1 : 0,
             (props & NIMBLE_PROPERTY::WRITE_NR) ? 1 : 0);

        g_printerConnected = true;
        LOGI(TAG, "BLE connected on attempt %d!", attempt);

        delay(500);
        raster_init();
        delay(200);

        if (bt_is_alive()) {
            LOGI(TAG, "Printer ready (BLE)");
            return true;
        } else {
            LOGW(TAG, "Connection dropped after init");
            g_printerConnected = false;
        }

        delay(BT_RETRY_DELAY_MS);
    }

    LOGE(TAG, "All %d BLE attempts failed", BT_CONNECT_RETRIES);
    g_printerConnected = false;
    return false;
}

void bt_disconnect() {
    LOGI(TAG, "Disconnecting BLE...");
    if (pBleClient && pBleClient->isConnected()) {
        pBleClient->disconnect();
    }
    pBleRxChar = nullptr;
    g_printerConnected = false;
    delay(300);
    LOGI(TAG, "Disconnected");
}

#endif

// =============================================================================
//  Растровые примитивы
// =============================================================================

void raster_init() {
    if (!g_printerConnected) return;
    LOGD(TAG, "Sending ESC @ (init)");
    const uint8_t cmd[] = {0x1B, 0x40};
    bt_write(cmd, 2);
    delay(100);  // увеличено для надёжности
}

void raster_feed(uint8_t lines) {
    if (!g_printerConnected) return;
    LOGD(TAG, "Feed %d lines", lines);
    const uint8_t cmd[] = {0x1B, 0x64, lines};
    bt_write(cmd, 3);
}

void raster_empty(int heightPx) {
    if (!g_printerConnected || heightPx <= 0) return;
    size_t sz = (size_t)PRINTER_WIDTH_BYTES * heightPx;
    uint8_t* buf = (uint8_t*)calloc(sz, 1);
    if (!buf) {
        LOGE(TAG, "OOM empty %d px", heightPx);
        return;
    }
    sendRasterBlock(buf, PRINTER_WIDTH_BYTES, heightPx);
    free(buf);
}

void raster_separator(char style, uint8_t heightPx) {
    if (!g_printerConnected) return;
    LOGD(TAG, "Separator style='%c' h=%d", style, heightPx);

    size_t sz = (size_t)PRINTER_WIDTH_BYTES * heightPx;
    uint8_t* buf = (uint8_t*)calloc(sz, 1);
    if (!buf) { LOGE(TAG, "OOM sep"); return; }

    for (int y = 0; y < heightPx; y++) {
        for (int x = 0; x < PRINTER_WIDTH_PX; x++) {
            bool on = false;
            switch (style) {
                case '-': on = ((x / 4) % 2 == 0);              break;
                case '=': on = (y == 0 || y == heightPx - 1);   break;
                default:  on = true;                             break;
            }
            if (on) {
                buf[y * PRINTER_WIDTH_BYTES + (x >> 3)] |= (0x80 >> (x & 7));
            }
        }
    }

    sendRasterBlock(buf, PRINTER_WIDTH_BYTES, heightPx);
    free(buf);
}

void raster_print_line(const char* text, uint8_t scale,
                       uint8_t align, bool bold) {
    if (!g_printerConnected || !text) return;

    int textLen = (int)strlen(text);
    if (textLen == 0) {
        raster_empty(FONT_CHAR_H * scale);
        return;
    }

    const int charW    = FONT_CHAR_W * scale;
    const int charH    = FONT_CHAR_H * scale;
    const int maxChars = PRINTER_WIDTH_PX / charW;
    if (textLen > maxChars) textLen = maxChars;

    int textPx  = textLen * charW;
    int offsetX = 0;
    if (align == 1) offsetX = (PRINTER_WIDTH_PX - textPx) / 2;
    if (align == 2) offsetX = PRINTER_WIDTH_PX - textPx;
    if (offsetX < 0) offsetX = 0;

    const int height = charH;
    size_t rasterSz  = (size_t)PRINTER_WIDTH_BYTES * height;
    uint8_t* raster  = (uint8_t*)calloc(rasterSz, 1);
    if (!raster) {
        LOGE(TAG, "OOM raster %d bytes", rasterSz);
        return;
    }

    LOGD(TAG, "Render: \"%.*s\" scale=%d align=%d bold=%d offset=%d",
         textLen, text, scale, align, bold, offsetX);

    for (int ci = 0; ci < textLen; ci++) {
        const uint8_t* glyph = fontGlyph(text[ci]);

        for (int row = 0; row < FONT_CHAR_H; row++) {
            uint8_t fb = pgm_read_byte(&glyph[row]);
            if (bold) fb |= (fb >> 1);

            for (int col = 0; col < FONT_CHAR_W; col++) {
                if (!((fb >> (7 - col)) & 1)) continue;

                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        int px = offsetX + ci * charW + col * scale + sx;
                        int py = row * scale + sy;
                        if (px < 0 || px >= PRINTER_WIDTH_PX || py >= height)
                            continue;
                        raster[py * PRINTER_WIDTH_BYTES + (px >> 3)]
                            |= (0x80 >> (px & 7));
                    }
                }
            }
        }
    }

    bool ok = sendRasterBlock(raster, PRINTER_WIDTH_BYTES, height);
    free(raster);

    if (!ok) {
        LOGE(TAG, "Print line FAILED");
    }
}

// =============================================================================
//  Печать талона
// =============================================================================
void printTicket(const char* num, const char* queue, const char* eta) {
    if (!g_printerConnected) {
        LOGE("PRN", "Cannot print: not connected");
        return;
    }
    if (!bt_is_alive()) {
        LOGE("PRN", "Cannot print: connection dead");
        g_printerConnected = false;
        return;
    }

    LOGI("PRN", "=== Printing ticket: %s ===", num);
    LOGI("PRN", "  Queue: %s", queue);
    LOGI("PRN", "  ETA:   %s", (eta && eta[0]) ? eta : "(none)");

    raster_init();
    delay(150);

    LOGI("PRN", "Step 1/7: Header");
    raster_print_line("SMART QUEUE", 2, 1, true);
    raster_empty(6);

    LOGI("PRN", "Step 2/7: Separator");
    raster_separator('-', 2);
    raster_empty(6);

    LOGI("PRN", "Step 3/7: Queue name");
    raster_print_line(queue, 1, 1, false);
    raster_empty(6);

    LOGI("PRN", "Step 4/7: Ticket number");
    raster_print_line(num, 2, 1, true);
    raster_empty(6);

    if (eta && eta[0]) {
        LOGI("PRN", "Step 5/7: ETA");
        char waitBuf[64];
        snprintf(waitBuf, sizeof(waitBuf), "Wait: %s", eta);
        raster_print_line(waitBuf, 1, 0, false);
        raster_empty(6);
    } else {
        LOGD("PRN", "Step 5/7: ETA skipped");
    }

    LOGI("PRN", "Step 6/7: Bottom separator");
    raster_separator('-', 2);

    LOGI("PRN", "Step 7/7: Feed");
    raster_feed(4);

    LOGI("PRN", "=== Ticket %s printed OK ===", num);
}