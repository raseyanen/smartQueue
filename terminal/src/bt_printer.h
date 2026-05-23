#pragma once
/**
 * ============================================================================
 *  bt_printer.h — Bluetooth + растровая печать FunnyPrint
 * ============================================================================
 */

#include "config.h"

// ── Подключение ─────────────────────────────────────────────────────────────
bool bt_connect();             // с retry
void bt_disconnect();
bool bt_is_alive();            // проверка что соединение живое

// ── Низкоуровневая отправка ─────────────────────────────────────────────────
// Возвращает false при ошибке отправки
bool bt_write(const uint8_t* data, size_t len);

// ── Растровые команды ───────────────────────────────────────────────────────
void raster_init();
void raster_feed(uint8_t lines = 3);
void raster_print_line(const char* text, uint8_t scale = 1,
                       uint8_t align = 0, bool bold = false);
void raster_separator(char style = '-', uint8_t heightPx = 2);
void raster_empty(int heightPx);

// ── Талон ───────────────────────────────────────────────────────────────────
void printTicket(const char* num, const char* queue, const char* eta);