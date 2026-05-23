#pragma once
#include "config.h"
#include <SettingsESP.h>

void ui_build(sets::Builder& b);
void ui_update(sets::Updater& upd);

// Регистрирует HTTP-эндпоинты /log, /log/clear, /log/raw
// Вызвать ПОСЛЕ sett.begin()
void ui_register_log_endpoint();