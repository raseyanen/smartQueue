#pragma once
/**
 * ============================================================================
 *  bt_printer.h — Bluetooth подключение + растровая печать FunnyPrint
 * ============================================================================
 *  Протокол из rastertozj.c (printer-driver-funnyprint):
 *    Команда:   0x1D 0x76 0x30 0x00
 *    Заголовок: wL wH hL hH   (ширина в байтах, высота в строках, LE)
 *    Данные:    h × w байт    (MSB слева, 1 = чёрный)
 * ============================================================================
 */

#include "config.h"

// ── Bluetooth подключение ───────────────────────────────────────────────────
bool bt_connect();
void bt_disconnect();

// ── Низкоуровневая отправка ─────────────────────────────────────────────────
void bt_write(const uint8_t* data, size_t len);

// ── Растровая печать ────────────────────────────────────────────────────────

// Инициализация принтера (ESC @)
void raster_init();

// Протяжка бумаги (ESC d n)
void raster_feed(uint8_t lines = 3);

// Печать строки текста
//   scale: 1 = 8x16, 2 = 16x32
//   align: 0 = лево, 1 = центр, 2 = право
void raster_print_line(const char* text, uint8_t scale = 1,
                       uint8_t align = 0, bool bold = false);

// Горизонтальный разделитель
//   style: '-' штриховая, '=' двойная, '_' сплошная
void raster_separator(char style = '-', uint8_t heightPx = 2);

// Пустое пространство
void raster_empty(int heightPx);

// ── Печать талона ───────────────────────────────────────────────────────────
void printTicket(const char* num, const char* queue, const char* eta);