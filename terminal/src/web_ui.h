#pragma once
/**
 * ============================================================================
 *  web_ui.h — построитель веб-интерфейса Settings
 * ============================================================================
 */

#include "config.h"
#include <SettingsESP.h>

void ui_build(sets::Builder& b);
void ui_update(sets::Updater& upd);