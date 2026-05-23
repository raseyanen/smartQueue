#include "bt_printer.h"
#include "raster_font.h"
#include <stdlib.h>  // calloc, free

#if BT_CLASSIC
    #include <BluetoothSerial.h>
    extern BluetoothSerial SerialBT;
#else
    #include <NimBLEDevice.h>
    extern NimBLEClient*                pBleClient;
    extern NimBLERemoteCharacteristic*  pBleRxChar;
#endif

// =============================================================================
//  Низкоуровневая отправка
// =============================================================================
void bt_write(const uint8_t* d, size_t n) {
    if (!g_printerConnected || !d || n == 0) return;

#if BT_CLASSIC
    if (!SerialBT.connected()) return;
    for (size_t off = 0; off < n; off += SPP_CHUNK_SIZE) {
        size_t chunk = min((size_t)SPP_CHUNK_SIZE, n - off);
        SerialBT.write(d + off, chunk);
        if (n - off > SPP_CHUNK_SIZE) delay(SPP_CHUNK_DELAY_MS);
    }
#else
    if (!pBleRxChar) return;
    for (size_t off = 0; off < n; off += BLE_CHUNK_SIZE) {
        size_t chunk = min((size_t)BLE_CHUNK_SIZE, n - off);
        pBleRxChar->writeValue(d + off, chunk, true);
        delay(BLE_CHUNK_DELAY_MS);
    }
#endif
}

// =============================================================================
//  Отправка растрового блока (протокол FunnyPrint / rastertozj.c)
//
//  Формат:
//    1D 76 30 00   — Print Raster Bit Image
//    wL wH         — ширина в байтах (little-endian)
//    hL hH         — высота блока (little-endian)
//    DATA          — h × w байт (MSB first, 1 = чёрный)
// =============================================================================
static void sendRasterBlock(const uint8_t* data, int widthBytes, int height) {
    for (int y = 0; y < height; y += RASTER_BLOCK_HEIGHT) {
        int blockH = min(RASTER_BLOCK_HEIGHT, height - y);

        uint8_t hdr[] = {
            0x1D, 0x76, 0x30, 0x00,
            (uint8_t)(widthBytes & 0xFF),
            (uint8_t)((widthBytes >> 8) & 0xFF),
            (uint8_t)(blockH & 0xFF),
            (uint8_t)((blockH >> 8) & 0xFF)
        };
        bt_write(hdr, sizeof(hdr));
        bt_write(data + y * widthBytes, blockH * widthBytes);

        // Пауза на обработку принтером
        delay(blockH / 4 + 10);
    }
}

// =============================================================================
//  Bluetooth-подключение
// =============================================================================
#if BT_CLASSIC

bool bt_connect() {
    // Читаем MAC из БД
    char macBuf[20];
    {
        // Копируем из GyverDB — она возвращает AnyValue
        auto v = g_db[K_BT_MAC];
        const char* s = v.toString().c_str();
        size_t len = strlen(s);
        if (len < 17) {
            strncpy(macBuf, DEFAULT_PRINTER_MAC, sizeof(macBuf));
        } else {
            strncpy(macBuf, s, sizeof(macBuf));
        }
        macBuf[sizeof(macBuf) - 1] = '\0';
    }

    uint8_t addr[6];
    if (sscanf(macBuf, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
               &addr[0], &addr[1], &addr[2], &addr[3], &addr[4], &addr[5]) != 6) {
        Serial.println(F("[BT] Bad MAC"));
        return false;
    }

    SerialBT.end();
    delay(200);
    if (!SerialBT.begin(BT_NAME_CLASSIC, true)) {
        Serial.println(F("[BT] Init failed"));
        return false;
    }

    bool ok = SerialBT.connect(addr);
    g_printerConnected = ok;
    if (ok) {
        delay(200);
        raster_init();
        Serial.println(F("[BT] Connected (SPP)"));
    } else {
        Serial.println(F("[BT] Connect failed"));
    }
    return ok;
}

void bt_disconnect() {
    SerialBT.disconnect();
    g_printerConnected = false;
    delay(200);
}

#else  // BLE (C3)

class BtPrinterCallbacks : public NimBLEClientCallbacks {
    void onDisconnect(NimBLEClient*) override {
        g_printerConnected = false;
        Serial.println(F("[BLE] Disconnected"));
    }
};

bool bt_connect() {
    char macBuf[20];
    {
        auto v = g_db[K_BT_MAC];
        const char* s = v.toString().c_str();
        size_t len = strlen(s);
        if (len < 17) {
            strncpy(macBuf, DEFAULT_PRINTER_MAC, sizeof(macBuf));
        } else {
            strncpy(macBuf, s, sizeof(macBuf));
        }
        macBuf[sizeof(macBuf) - 1] = '\0';
    }

    NimBLEDevice::init(BT_NAME_BLE);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);

    pBleClient = NimBLEDevice::createClient();
    static BtPrinterCallbacks cbk;
    pBleClient->setClientCallbacks(&cbk, false);

    // PUBLIC → RANDOM fallback
    bool ok = pBleClient->connect(NimBLEAddress(macBuf, BLE_ADDR_PUBLIC));
    if (!ok) {
        Serial.println(F("[BLE] PUBLIC failed, trying RANDOM..."));
        ok = pBleClient->connect(NimBLEAddress(macBuf, BLE_ADDR_RANDOM));
    }
    if (!ok) {
        Serial.println(F("[BLE] Connect failed"));
        return false;
    }

    pBleClient->setMTU(512);

    auto* svc = pBleClient->getService(NUS_SVC_UUID);
    if (!svc) {
        pBleClient->disconnect();
        Serial.println(F("[BLE] No NUS service"));
        return false;
    }
    pBleRxChar = svc->getCharacteristic(NUS_RX_UUID);
    if (!pBleRxChar) {
        pBleClient->disconnect();
        Serial.println(F("[BLE] No RX char"));
        return false;
    }

    g_printerConnected = true;
    delay(200);
    raster_init();
    Serial.println(F("[BLE] Connected (NUS)"));
    return true;
}

void bt_disconnect() {
    if (pBleClient && pBleClient->isConnected()) {
        pBleClient->disconnect();
    }
    g_printerConnected = false;
    delay(200);
}

#endif

// =============================================================================
//  Примитивы растровой печати
// =============================================================================

void raster_init() {
    if (!g_printerConnected) return;
    const uint8_t cmd[] = {0x1B, 0x40};   // ESC @
    bt_write(cmd, 2);
    delay(50);
}

void raster_feed(uint8_t lines) {
    if (!g_printerConnected) return;
    const uint8_t cmd[] = {0x1B, 0x64, lines};  // ESC d n
    bt_write(cmd, 3);
}

void raster_empty(int heightPx) {
    if (!g_printerConnected || heightPx <= 0) return;
    size_t sz = (size_t)PRINTER_WIDTH_BYTES * heightPx;
    uint8_t* buf = (uint8_t*)calloc(sz, 1);
    if (!buf) return;
    sendRasterBlock(buf, PRINTER_WIDTH_BYTES, heightPx);
    free(buf);
}

void raster_separator(char style, uint8_t heightPx) {
    if (!g_printerConnected) return;
    size_t sz = (size_t)PRINTER_WIDTH_BYTES * heightPx;
    uint8_t* buf = (uint8_t*)calloc(sz, 1);
    if (!buf) return;

    for (int y = 0; y < heightPx; y++) {
        for (int x = 0; x < PRINTER_WIDTH_PX; x++) {
            bool on = false;
            switch (style) {
                case '-': on = ((x / 4) % 2 == 0);                   break;
                case '=': on = (y == 0 || y == heightPx - 1);        break;
                default:  on = true;                                  break;
            }
            if (on) {
                int bi = y * PRINTER_WIDTH_BYTES + (x >> 3);
                buf[bi] |= (0x80 >> (x & 7));
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

    // Смещение для выравнивания
    int textPx  = textLen * charW;
    int offsetX = 0;
    if (align == 1) offsetX = (PRINTER_WIDTH_PX - textPx) / 2;
    if (align == 2) offsetX = PRINTER_WIDTH_PX - textPx;
    if (offsetX < 0) offsetX = 0;

    const int height = charH;
    size_t rasterSz  = (size_t)PRINTER_WIDTH_BYTES * height;
    uint8_t* raster  = (uint8_t*)calloc(rasterSz, 1);
    if (!raster) { Serial.println(F("[Raster] OOM")); return; }

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
                        int bi = py * PRINTER_WIDTH_BYTES + (px >> 3);
                        raster[bi] |= (0x80 >> (px & 7));
                    }
                }
            }
        }
    }

    sendRasterBlock(raster, PRINTER_WIDTH_BYTES, height);
    free(raster);
}

// =============================================================================
//  Печать талона
// =============================================================================
void printTicket(const char* num, const char* queue, const char* eta) {
    if (!g_printerConnected) {
        Serial.println(F("[Printer] not connected"));
        return;
    }
    Serial.printf("[Printer] Ticket %s\n", num);

    raster_init();
    delay(100);

    // Заголовок 2x, центр, жирный
    raster_print_line("SMART QUEUE", 2, 1, true);
    raster_empty(4);

    raster_separator('-', 2);
    raster_empty(4);

    // Очередь 1x, центр
    raster_print_line(queue, 1, 1, false);
    raster_empty(4);

    // Номер 2x, центр, жирный
    raster_print_line(num, 2, 1, true);
    raster_empty(4);

    // Ожидание
    if (eta && eta[0]) {
        char waitBuf[64];
        snprintf(waitBuf, sizeof(waitBuf), "Wait: %s", eta);
        raster_print_line(waitBuf, 1, 0, false);
        raster_empty(4);
    }

    raster_separator('-', 2);
    raster_feed(4);

    Serial.printf("[Printer] Ticket %s OK\n", num);
}