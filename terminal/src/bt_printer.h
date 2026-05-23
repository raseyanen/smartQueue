#pragma once
#include "config.h"

bool bt_connect();
void bt_disconnect();
bool bt_is_alive();

// Отправка сырых данных (чанкованная)
bool bt_write(const uint8_t* data, size_t len);

// Растровая печать (протокол FunnyPrint)
void raster_init();
void raster_feed(uint8_t lines = 3);
void raster_set_concentration(uint8_t level = 4);  // 1-6, нагрев
void raster_print_line(const char* text, uint8_t scale = 1,
                       uint8_t align = 0, bool bold = false);
void raster_separator(char style = '-', uint8_t heightPx = 2);
void raster_empty(int heightPx);

// Талон
void printTicket(const char* num, const char* queue, const char* eta);