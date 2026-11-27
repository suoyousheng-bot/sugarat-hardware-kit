/* Copyright (c) 2026 Deotaland. SPDX-License-Identifier: Apache-2.0 */
#include "lighting_tx.h"

#include "esp_log.h"
#include "esp_now.h"
#include "esp_random.h"
#include "servo_radio.h"

_Static_assert((int)SERVO_RADIO_START_CHANNEL == (int)LIGHTING_CHANNEL, "Lighting and servo radio must share a channel");

static const uint8_t BROADCAST[] = {255, 255, 255, 255, 255, 255};
static const char *TAG = "reactor_lighting";
static struct {
    bool ready;
    bool sent;
    uint32_t last_ms;
    uint32_t retry_ms;
    lighting_frame_t frame;
} tx;

void lighting_tx_poll(uint32_t now, lighting_style_t style, uint8_t level, bool animating)
{
    if (!tx.ready) {
        if ((int32_t)(now - tx.retry_ms) < 0) return;
        tx.retry_ms = now + LIGHTING_HEARTBEAT_MS;
        const esp_now_peer_info_t peer = {
            .peer_addr = {255, 255, 255, 255, 255, 255},
            .ifidx = WIFI_IF_STA,
            .channel = LIGHTING_CHANNEL,
        };
        const esp_err_t err = esp_now_add_peer(&peer);
        if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
            if (err != ESP_ERR_ESPNOW_NOT_INIT) ESP_LOGW(TAG, "Lighting peer: %s", esp_err_to_name(err));
            return;
        }
        tx.ready = true;
        tx.frame.boot = esp_random();
    }

    const unsigned interval = (animating || tx.frame.level != level) ? LIGHTING_ANIMATION_MS : LIGHTING_HEARTBEAT_MS;
    if (tx.sent && style == tx.frame.style && now - tx.last_ms < interval) return;
    tx.last_ms = now;
    tx.frame.style = style;
    tx.frame.level = level;
    ++tx.frame.sequence;

    uint8_t data[LIGHTING_PACKET_BYTES];
    lighting_encode(data, &tx.frame, SUGARAT_LIGHTING_GROUP);
    const esp_err_t err = esp_now_send(BROADCAST, data, sizeof data);
    tx.sent = true;
    if (err != ESP_OK) {
        // Next snapshot supersedes a failed send. This broadcast never delays servo polling.
        ESP_LOGD(TAG, "Lighting snapshot dropped: %s", esp_err_to_name(err));
    }
}
