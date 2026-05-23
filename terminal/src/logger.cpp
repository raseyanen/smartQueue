#include "logger.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

// ── Кольцевой буфер ─────────────────────────────────────────────────────────
static char    logBuf[LOG_MAX_LINES][LOG_LINE_LEN + 1];
static int     logHead  = 0;   // следующая позиция для записи
static int     logTotal = 0;   // всего записей (для count)
static bool    logInit  = false;

// Статический буфер для getLogText — собирается при запросе
// Макс размер: LOG_MAX_LINES * (LOG_LINE_LEN + 1(newline))
// Это ~10 КБ при 80 строк × 121 — выделяем на куче один раз
static char*   logTextBuf = nullptr;
#define LOG_TEXT_BUF_SIZE  (LOG_MAX_LINES * (LOG_LINE_LEN + 2) + 1)

static const char* LVL_NAMES[] = {"DBG", "INF", "WRN", "ERR"};

void log_init() {
    memset(logBuf, 0, sizeof(logBuf));
    logHead  = 0;
    logTotal = 0;
    logInit  = true;

    if (!logTextBuf) {
        logTextBuf = (char*)malloc(LOG_TEXT_BUF_SIZE);
        if (logTextBuf) logTextBuf[0] = '\0';
    }
}

void log_add(LogLevel lvl, const char* tag, const char* fmt, ...) {
    if (!logInit) log_init();

    char msgPart[LOG_LINE_LEN - 30];  // место под метку+тег+уровень
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msgPart, sizeof(msgPart), fmt, ap);
    va_end(ap);

    // Формат: "12345 [INF][BT  ] message..."
    unsigned long ms = millis();
    snprintf(logBuf[logHead], LOG_LINE_LEN + 1,
             "%7lu [%s][%-5s] %s",
             ms, LVL_NAMES[lvl], tag ? tag : "?", msgPart);

    // В Serial тоже
    Serial.println(logBuf[logHead]);

    logHead = (logHead + 1) % LOG_MAX_LINES;
    logTotal++;
}

int log_count() {
    return (logTotal < LOG_MAX_LINES) ? logTotal : LOG_MAX_LINES;
}

void log_clear() {
    memset(logBuf, 0, sizeof(logBuf));
    logHead  = 0;
    logTotal = 0;
}

const char* log_get_text() {
    if (!logTextBuf) return "OOM";

    logTextBuf[0] = '\0';
    int count = log_count();
    if (count == 0) {
        strcpy(logTextBuf, "(empty)");
        return logTextBuf;
    }

    // Определяем начало кольца
    int start;
    if (logTotal < LOG_MAX_LINES) {
        start = 0;
    } else {
        start = logHead;  // самая старая запись
    }

    char* p = logTextBuf;
    size_t remain = LOG_TEXT_BUF_SIZE - 1;

    for (int i = 0; i < count && remain > 2; i++) {
        int idx = (start + i) % LOG_MAX_LINES;
        size_t len = strlen(logBuf[idx]);
        if (len > remain - 1) len = remain - 1;
        memcpy(p, logBuf[idx], len);
        p += len;
        *p++ = '\n';
        remain -= (len + 1);
    }
    *p = '\0';

    return logTextBuf;
}