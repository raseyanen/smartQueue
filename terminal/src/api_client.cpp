#include "api_client.h"
#include "bt_printer.h"
#include "logger.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <mbedtls/md.h>

#define TAG "API"

static void hmacSha256(const char* key, size_t keyLen,
                       const char* msg, size_t msgLen,
                       char* outHex) {
    uint8_t raw[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&ctx, (const uint8_t*)key, keyLen);
    mbedtls_md_hmac_update(&ctx, (const uint8_t*)msg, msgLen);
    mbedtls_md_hmac_finish(&ctx, raw);
    mbedtls_md_free(&ctx);
    for (int i = 0; i < 32; i++) sprintf(outHex + i*2, "%02x", raw[i]);
    outHex[64] = '\0';
}

static bool jsonStr(const char* json, const char* key,
                    char* dst, size_t maxLen) {
    dst[0] = '\0';
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\":\"", key);
    const char* p = strstr(json, pat);
    if (!p) return false;
    p += strlen(pat);
    const char* e = strchr(p, '"');
    if (!e) return false;
    size_t len = (size_t)(e - p);
    if (len >= maxLen) len = maxLen - 1;
    memcpy(dst, p, len);
    dst[len] = '\0';
    return true;
}

void api_poll() {
    if (!g_wifiConnected) return;

    char urlBuf[128]={0}, queueBuf[48]={0}, tokenBuf[128]={0}, secretBuf[128]={0};
    { auto v = g_db[K_API_URL];    strncpy(urlBuf,    v.toString().c_str(), sizeof(urlBuf)-1);    }
    { auto v = g_db[K_API_QUEUE];  strncpy(queueBuf,  v.toString().c_str(), sizeof(queueBuf)-1);  }
    { auto v = g_db[K_API_TOKEN];  strncpy(tokenBuf,  v.toString().c_str(), sizeof(tokenBuf)-1);  }
    { auto v = g_db[K_API_SECRET]; strncpy(secretBuf, v.toString().c_str(), sizeof(secretBuf)-1); }

    if (!urlBuf[0] || !queueBuf[0]) {
        LOGD(TAG, "URL or Queue empty, skip");
        return;
    }

    char fullUrl[256];
    snprintf(fullUrl, sizeof(fullUrl), "%s/api/queues/%s/next_ticket/",
             urlBuf, queueBuf);

    LOGI(TAG, "GET %s", fullUrl);

    WiFiClient wc;
    HTTPClient http;
    http.begin(wc, fullUrl);
    http.setTimeout(5000);

    if (tokenBuf[0]) {
        char authHdr[160];
        snprintf(authHdr, sizeof(authHdr), "Token %s", tokenBuf);
        http.addHeader("Authorization", authHdr);
        LOGD(TAG, "Auth header set");
    }

    if (secretBuf[0]) {
        char msg[300], sigHex[65], sigHdr[128];
        snprintf(msg, sizeof(msg), "GET:%s", fullUrl);
        hmacSha256(secretBuf, strlen(secretBuf), msg, strlen(msg), sigHex);
        snprintf(sigHdr, sizeof(sigHdr), "HMAC-SHA256 %s", sigHex);
        http.addHeader("X-Signature", sigHdr);
        LOGD(TAG, "HMAC signature set");
    }

    http.addHeader("Content-Type", "application/json");
    int code = http.GET();

    if (code > 0) {
        LOGI(TAG, "Response: %d", code);
    } else {
        LOGE(TAG, "HTTP error: %d (%s)", code, http.errorToString(code).c_str());
        http.end();
        return;
    }

    if (code == 200) {
        String body = http.getString();
        LOGI(TAG, "Body: %s", body.c_str());

        char num[32]={0}, queue[64]={0}, eta[32]={0};
        jsonStr(body.c_str(), "number", num, sizeof(num));
        jsonStr(body.c_str(), "queue", queue, sizeof(queue));
        jsonStr(body.c_str(), "eta", eta, sizeof(eta));

        if (!num[0]) strncpy(num, body.c_str(), sizeof(num)-1);
        if (!queue[0]) strcpy(queue, "Queue");

        LOGI(TAG, "Parsed: num=%s queue=%s eta=%s", num, queue, eta);
        printTicket(num, queue, eta);
    } else if (code == 204 || code == 404) {
        LOGD(TAG, "No tickets available (%d)", code);
    } else {
        LOGW(TAG, "Unexpected response %d", code);
    }

    http.end();
}