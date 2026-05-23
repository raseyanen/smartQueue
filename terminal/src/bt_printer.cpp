#include "bt_printer.h"
#include "raster_font.h"
#include "logger.h"
#include <stdlib.h>

#define TAG "BT"

#if BT_CLASSIC
    #include <BluetoothSerial.h>
    extern BluetoothSerial SerialBT;
    static bool sppInitialized = false;
#else
    #include <NimBLEDevice.h>
    extern NimBLEClient*                pBleClient;
    extern NimBLERemoteCharacteristic*  pBleRxChar;
    static bool bleInitialized = false;
#endif

// =============================================================================
//  Проверка живости
// =============================================================================
bool bt_is_alive() {
#if BT_CLASSIC
    return sppInitialized && SerialBT.connected();
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
        LOGW(TAG, "write skip: conn=%d data=%p n=%u",
             g_printerConnected, d, n);
        return false;
    }

    if (!bt_is_alive()) {
        LOGE(TAG, "write: connection dead");
        g_printerConnected = false;
        return false;
    }

#if BT_CLASSIC
    for (size_t off = 0; off < n; off += SPP_CHUNK_SIZE) {
        size_t chunk = min((size_t)SPP_CHUNK_SIZE, n - off);
        size_t written = SerialBT.write(d + off, chunk);
        if (written != chunk) {
            LOGE(TAG, "SPP write err: %u/%u at %u", written, chunk, off);
            return false;
        }
        if (n > SPP_CHUNK_SIZE) {
            delay(SPP_CHUNK_DELAY_MS);
        }
    }
    return true;
#else
    for (size_t off = 0; off < n; off += BLE_CHUNK_SIZE) {
        size_t chunk = min((size_t)BLE_CHUNK_SIZE, n - off);
        if (!pBleRxChar->writeValue(d + off, chunk, true)) {
            LOGE(TAG, "BLE write err at %u", off);
            return false;
        }
        delay(BLE_CHUNK_DELAY_MS);
    }
    return true;
#endif
}

// =============================================================================
//  Отправка растрового блока (протокол FunnyPrint)
// =============================================================================
static bool sendRasterBlock(const uint8_t* data, int widthBytes, int height) {
    LOGD(TAG, "Raster: %dx%d = %d bytes",
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

        if (!bt_write(hdr, sizeof(hdr))) {
            LOGE(TAG, "Raster header failed y=%d", y);
            return false;
        }
        if (!bt_write(data + y * widthBytes, blockH * widthBytes)) {
            LOGE(TAG, "Raster data failed y=%d", y);
            return false;
        }

        // Принтер должен обработать блок
        int waitMs = blockH * 2 + 30;
        delay(waitMs);
    }
    return true;
}

// =============================================================================
//  Чтение MAC из БД
// =============================================================================
static void readMac(char* buf, size_t sz) {
    auto v = g_db[K_BT_MAC];
    const char* s = v.toString().c_str();
    if (strlen(s) < 17) {
        strncpy(buf, DEFAULT_PRINTER_MAC, sz - 1);
        LOGW(TAG, "MAC not set, default: %s", DEFAULT_PRINTER_MAC);
    } else {
        strncpy(buf, s, sz - 1);
    }
    buf[sz - 1] = '\0';
}

// =============================================================================
//  Classic SPP подключение — БЕЗ реинициализации в цикле retry
// =============================================================================
#if BT_CLASSIC

// Инициализация BT стека — вызывается ОДИН РАЗ
static bool ensureSppInit() {
    if (sppInitialized) return true;

    LOGI(TAG, "Initializing BT Classic stack...");

    if (!SerialBT.begin(BT_NAME_CLASSIC, true)) {
        LOGE(TAG, "SerialBT.begin() FAILED");
        return false;
    }

    sppInitialized = true;
    LOGI(TAG, "BT Classic stack ready");
    delay(500);  // дать стеку стабилизироваться
    return true;
}

bool bt_connect() {
    char macBuf[20];
    readMac(macBuf, sizeof(macBuf));

    uint8_t addr[6];
    if (sscanf(macBuf, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
               &addr[0], &addr[1], &addr[2], &addr[3],
               &addr[4], &addr[5]) != 6) {
        LOGE(TAG, "Bad MAC format: '%s'", macBuf);
        LOGE(TAG, "Expected XX:XX:XX:XX:XX:XX");
        return false;
    }

    LOGI(TAG, "Target: %02X:%02X:%02X:%02X:%02X:%02X",
         addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);

    // Инициализируем стек один раз
    if (!ensureSppInit()) {
        LOGE(TAG, "Cannot init BT stack");
        return false;
    }

    // Если уже подключены — отключаемся
    if (SerialBT.connected()) {
        LOGI(TAG, "Already connected, disconnecting first...");
        SerialBT.disconnect();
        delay(1000);
    }

    for (int attempt = 1; attempt <= BT_CONNECT_RETRIES; attempt++) {
        LOGI(TAG, "SPP connect attempt %d/%d ...", attempt, BT_CONNECT_RETRIES);

        // connect() с таймаутом (блокирующий вызов ~5-10 сек)
        bool ok = SerialBT.connect(addr);

        // Даём время на установку соединения
        delay(500);

        if (ok && SerialBT.connected()) {
            g_printerConnected = true;
            LOGI(TAG, "SPP connected! (attempt %d)", attempt);

            // Длинная пауза — принтер инициализируется
            delay(1000);

            // Отправляем ESC @ (init printer)
            raster_init();
            delay(500);

            // Финальная проверка
            if (SerialBT.connected()) {
                LOGI(TAG, "Printer ready (SPP)");
                return true;
            } else {
                LOGW(TAG, "Connection dropped after init");
                g_printerConnected = false;
            }
        } else {
            LOGW(TAG, "SPP attempt %d failed", attempt);
            LOGW(TAG, "  connected()=%d", SerialBT.connected() ? 1 : 0);

            if (attempt == 1) {
                LOGW(TAG, "Hint: if printer is BLE-only, use C3 board");
                LOGW(TAG, "Hint: check MAC address is correct");
                LOGW(TAG, "Hint: ensure printer is ON and in range");
            }
        }

        if (attempt < BT_CONNECT_RETRIES) {
            LOGI(TAG, "Waiting %d ms before retry...", BT_RETRY_DELAY_MS);
            delay(BT_RETRY_DELAY_MS);
        }
    }

    LOGE(TAG, "All %d SPP attempts failed", BT_CONNECT_RETRIES);
    LOGE(TAG, "Possible causes:");
    LOGE(TAG, "  1. Wrong MAC address");
    LOGE(TAG, "  2. Printer is BLE-only (not Classic SPP)");
    LOGE(TAG, "  3. Printer is off or out of range");
    LOGE(TAG, "  4. Printer paired with another device");

    g_printerConnected = false;
    return false;
}

void bt_disconnect() {
    LOGI(TAG, "Disconnecting SPP...");
    if (SerialBT.connected()) {
        SerialBT.disconnect();
    }
    g_printerConnected = false;
    delay(500);
    // НЕ вызываем SerialBT.end() — стек остаётся инициализированным
    LOGI(TAG, "Disconnected (stack stays alive)");
}

#else
// =============================================================================
//  BLE NUS подключение
// =============================================================================

class BtPrinterCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* cli) override {
        LOGI("BLE", "onConnect callback fired");
    }
    void onDisconnect(NimBLEClient* cli) override {
        g_printerConnected = false;
        LOGW("BLE", "onDisconnect callback");
    }
};

static BtPrinterCallbacks bleCallbacks;

static bool ensureBleInit() {
    if (bleInitialized) return true;

    LOGI(TAG, "Initializing NimBLE stack...");
    NimBLEDevice::init(BT_NAME_BLE);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    bleInitialized = true;
    LOGI(TAG, "NimBLE ready");
    delay(300);
    return true;
}

bool bt_connect() {
    char macBuf[20];
    readMac(macBuf, sizeof(macBuf));

    LOGI(TAG, "BLE target: %s", macBuf);

    if (!ensureBleInit()) return false;

    for (int attempt = 1; attempt <= BT_CONNECT_RETRIES; attempt++) {
        LOGI(TAG, "BLE attempt %d/%d", attempt, BT_CONNECT_RETRIES);

        // Очистка предыдущего клиента
        if (pBleClient) {
            if (pBleClient->isConnected()) {
                LOGD(TAG, "Disconnecting old client...");
                pBleClient->disconnect();
                delay(500);
            }
            NimBLEDevice::deleteClient(pBleClient);
            pBleClient = nullptr;
            pBleRxChar = nullptr;
            delay(300);
        }

        pBleClient = NimBLEDevice::createClient();
        pBleClient->setClientCallbacks(&bleCallbacks, false);
        pBleClient->setConnectTimeout(10);

        // Пробуем PUBLIC адрес
        LOGD(TAG, "Trying PUBLIC address...");
        bool connected = pBleClient->connect(
            NimBLEAddress(macBuf, BLE_ADDR_PUBLIC));

        if (!connected) {
            LOGD(TAG, "PUBLIC failed, trying RANDOM...");
            // Нужно пересоздать клиент для новой попытки
            NimBLEDevice::deleteClient(pBleClient);
            delay(200);
            pBleClient = NimBLEDevice::createClient();
            pBleClient->setClientCallbacks(&bleCallbacks, false);
            pBleClient->setConnectTimeout(10);
            connected = pBleClient->connect(
                NimBLEAddress(macBuf, BLE_ADDR_RANDOM));
        }

        if (!connected) {
            LOGW(TAG, "BLE link failed (attempt %d)", attempt);
            if (attempt == 1) {
                LOGW(TAG, "Hint: check MAC, ensure printer is ON");
            }
            NimBLEDevice::deleteClient(pBleClient);
            pBleClient = nullptr;
            delay(BT_RETRY_DELAY_MS);
            continue;
        }

        LOGI(TAG, "BLE link established");
        delay(500);

        // MTU
        uint16_t mtu = pBleClient->getMTU();
        LOGD(TAG, "MTU: %u", mtu);

        // Сервисы
        LOGD(TAG, "Discovering services...");
        auto* svcs = pBleClient->getServices(true);
        if (svcs) {
            LOGI(TAG, "Found %d services:", svcs->size());
            for (auto& s : *svcs) {
                LOGI(TAG, "  SVC: %s", s->getUUID().toString().c_str());
            }
        }

        auto* svc = pBleClient->getService(NUS_SVC_UUID);
        if (!svc) {
            LOGE(TAG, "NUS service NOT found");
            LOGE(TAG, "This printer may not use NUS protocol");
            pBleClient->disconnect();
            NimBLEDevice::deleteClient(pBleClient);
            pBleClient = nullptr;
            delay(BT_RETRY_DELAY_MS);
            continue;
        }
        LOGI(TAG, "NUS service found!");

        // Характеристики
        auto chars = svc->getCharacteristics(true);
        if (chars) {
            LOGD(TAG, "NUS characteristics (%d):", chars->size());
            for (auto& c : *chars) {
                LOGD(TAG, "  CHAR: %s props=0x%02X",
                     c->getUUID().toString().c_str(),
                     c->getProperties());
            }
        }

        pBleRxChar = svc->getCharacteristic(NUS_RX_UUID);
        if (!pBleRxChar) {
            LOGE(TAG, "RX characteristic NOT found");
            pBleClient->disconnect();
            NimBLEDevice::deleteClient(pBleClient);
            pBleClient = nullptr;
            delay(BT_RETRY_DELAY_MS);
            continue;
        }

        uint32_t props = pBleRxChar->getProperties();
        LOGI(TAG, "RX char OK (props=0x%02X write=%d writeNR=%d)",
             props,
             (props & NIMBLE_PROPERTY::WRITE) ? 1 : 0,
             (props & NIMBLE_PROPERTY::WRITE_NR) ? 1 : 0);

        g_printerConnected = true;

        delay(500);
        raster_init();
        delay(500);

        if (pBleClient->isConnected()) {
            LOGI(TAG, "Printer ready (BLE)");
            return true;
        } else {
            LOGW(TAG, "Connection dropped after init");
            g_printerConnected = false;
            pBleRxChar = nullptr;
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
    delay(500);
    // НЕ удаляем клиент и НЕ деинициализируем NimBLE
    LOGI(TAG, "Disconnected (stack stays)");
}

#endif

// =============================================================================
//  Растровые примитивы
// =============================================================================

void raster_init() {
    if (!g_printerConnected) return;
    LOGD(TAG, "ESC @ (init printer)");
    const uint8_t cmd[] = {0x1B, 0x40};
    bt_write(cmd, 2);
    delay(100);
}

void raster_feed(uint8_t lines) {
    if (!g_printerConnected) return;
    LOGD(TAG, "Feed %d", lines);
    const uint8_t cmd[] = {0x1B, 0x64, lines};
    bt_write(cmd, 3);
}

void raster_empty(int heightPx) {
    if (!g_printerConnected || heightPx <= 0) return;
    size_t sz = (size_t)PRINTER_WIDTH_BYTES * heightPx;
    uint8_t* buf = (uint8_t*)calloc(sz, 1);
    if (!buf) { LOGE(TAG, "OOM empty %d", heightPx); return; }
    sendRasterBlock(buf, PRINTER_WIDTH_BYTES, heightPx);
    free(buf);
}

void raster_separator(char style, uint8_t heightPx) {
    if (!g_printerConnected) return;
    size_t sz = (size_t)PRINTER_WIDTH_BYTES * heightPx;
    uint8_t* buf = (uint8_t*)calloc(sz, 1);
    if (!buf) { LOGE(TAG, "OOM sep"); return; }

    for (int y = 0; y < heightPx; y++) {
        for (int x = 0; x < PRINTER_WIDTH_PX; x++) {
            bool on = false;
            switch (style) {
                case '-': on = ((x / 4) % 2 == 0); break;
                case '=': on = (y == 0 || y == heightPx - 1); break;
                default:  on = true; break;
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
        LOGE(TAG, "OOM line %d bytes", rasterSz);
        return;
    }

    LOGD(TAG, "Render '%.*s' s=%d a=%d b=%d", textLen, text, scale, align, bold);

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
    if (!ok) LOGE(TAG, "print_line FAILED");
}

// =============================================================================
//  Печать талона
// =============================================================================
void printTicket(const char* num, const char* queue, const char* eta) {
    if (!g_printerConnected) {
        LOGE("PRN", "Not connected");
        return;
    }
    if (!bt_is_alive()) {
        LOGE("PRN", "Connection dead");
        g_printerConnected = false;
        return;
    }

    LOGI("PRN", "========== PRINTING TICKET ==========");
    LOGI("PRN", "  Number: %s", num);
    LOGI("PRN", "  Queue:  %s", queue);
    LOGI("PRN", "  ETA:    %s", (eta && eta[0]) ? eta : "(none)");
    LOGI("PRN", "  Heap:   %u", ESP.getFreeHeap());

    raster_init();
    delay(200);

    LOGI("PRN", "[1/7] Header 'SMART QUEUE'");
    raster_print_line("SMART QUEUE", 2, 1, true);
    raster_empty(6);

    LOGI("PRN", "[2/7] Separator");
    raster_separator('-', 2);
    raster_empty(6);

    LOGI("PRN", "[3/7] Queue name");
    raster_print_line(queue, 1, 1, false);
    raster_empty(6);

    LOGI("PRN", "[4/7] Number '%s'", num);
    raster_print_line(num, 2, 1, true);
    raster_empty(6);

    if (eta && eta[0]) {
        LOGI("PRN", "[5/7] ETA");
        char waitBuf[64];
        snprintf(waitBuf, sizeof(waitBuf), "Wait: %s", eta);
        raster_print_line(waitBuf, 1, 0, false);
        raster_empty(6);
    } else {
        LOGD("PRN", "[5/7] ETA skipped");
    }

    LOGI("PRN", "[6/7] Bottom separator");
    raster_separator('-', 2);

    LOGI("PRN", "[7/7] Feed");
    raster_feed(4);

    LOGI("PRN", "========== TICKET %s DONE ==========", num);
}