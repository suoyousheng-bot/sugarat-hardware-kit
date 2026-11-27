#include "servo_radio.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

enum { SEARCH_DWELL_MS = 420, FOLLOW_TIMEOUT_MS = 1500 };

typedef struct {
    uint8_t first_channel;
    uint8_t last_channel;
    uint8_t current_channel;
    uint8_t candidate;
    bool locked;
    TickType_t last_activity;
} channel_follower_t;

static const char *TAG = "servo_radio";
static const uint32_t PROTOCOL_ID = 0x33565253;
static uint8_t peer_address[ESP_NOW_ETH_ALEN];
static QueueHandle_t inbox;
static channel_follower_t follower;

_Static_assert(sizeof(servo_message_t) == 20, "Unexpected ESP-NOW packet layout");

static void receive_packet(const esp_now_recv_info_t *info, const uint8_t *data, int length)
{
    if (length != sizeof(servo_message_t) ||
        memcmp(info->src_addr, peer_address, sizeof(peer_address)) != 0) {
        return;
    }
    servo_message_t message;
    memcpy(&message, data, sizeof(message));
    if (message.protocol == PROTOCOL_ID && message.channel >= 1 && message.channel <= 14) {
        xQueueSend(inbox, &message, 0);
    }
}

static bool read_peer_mac(const char *peer_mac)
{
    return peer_mac && strlen(peer_mac) == 17 &&
           sscanf(peer_mac, "%2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx",
                  &peer_address[0], &peer_address[1], &peer_address[2],
                  &peer_address[3], &peer_address[4], &peer_address[5]) == 6;
}

static esp_err_t start_esp_now(const char *peer_mac)
{
    ESP_RETURN_ON_ERROR(esp_now_init(), TAG, "ESP-NOW initialization");

    // A zero peer channel sends on the interface's current channel.
    esp_now_peer_info_t peer = {.channel = 0, .ifidx = WIFI_IF_STA};
    memcpy(peer.peer_addr, peer_address, sizeof(peer_address));
    ESP_RETURN_ON_ERROR(esp_now_add_peer(&peer), TAG, "Add ESP-NOW peer");
    inbox = xQueueCreate(8, sizeof(servo_message_t));
    ESP_RETURN_ON_FALSE(inbox != NULL, ESP_ERR_NO_MEM, TAG, "ESP-NOW inbox");
    ESP_RETURN_ON_ERROR(esp_now_register_recv_cb(receive_packet), TAG, "ESP-NOW receive callback");

    uint8_t local[ESP_NOW_ETH_ALEN];
    ESP_RETURN_ON_ERROR(esp_wifi_get_mac(WIFI_IF_STA, local), TAG, "Read local MAC");
    ESP_LOGI(TAG, "ESP-NOW local %02X:%02X:%02X:%02X:%02X:%02X, peer %s",
             local[0], local[1], local[2], local[3], local[4], local[5], peer_mac);
    return ESP_OK;
}

esp_err_t servo_radio_attach(const char *peer_mac)
{
    // The Sugarat Wi-Fi owner has already started STA/APSTA and selects the home channel.
    ESP_RETURN_ON_FALSE(read_peer_mac(peer_mac), ESP_ERR_INVALID_ARG, TAG, "Invalid peer MAC address");
    return start_esp_now(peer_mac);
}

static uint8_t next_channel(uint8_t channel)
{
    return channel < follower.last_channel ? channel + 1 : follower.first_channel;
}

esp_err_t servo_radio_start(const char *peer_mac, servo_radio_mode_t mode)
{
    ESP_RETURN_ON_FALSE(read_peer_mac(peer_mac), ESP_ERR_INVALID_ARG, TAG, "Invalid peer MAC address");
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "Network initialization");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "Event loop");
    wifi_init_config_t wifi = WIFI_INIT_CONFIG_DEFAULT();
    wifi.nvs_enable = 0;
    ESP_RETURN_ON_ERROR(esp_wifi_init(&wifi), TAG, "Wi-Fi initialization");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "Wi-Fi storage");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "STA mode");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Wi-Fi start");
    ESP_RETURN_ON_ERROR(esp_wifi_set_ps(WIFI_PS_NONE), TAG, "Wi-Fi power save");
    uint8_t channel = SERVO_RADIO_START_CHANNEL;
    if (mode == SERVO_RADIO_FOLLOW_PEER) {
        wifi_country_t country;
        ESP_RETURN_ON_ERROR(esp_wifi_get_country(&country), TAG, "ESP-NOW channel range");
        follower.first_channel = country.schan;
        follower.last_channel = country.schan + country.nchan - 1;
        if (channel < follower.first_channel || channel > follower.last_channel) {
            channel = follower.first_channel;
        }
    }
    ESP_RETURN_ON_ERROR(esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE), TAG, "ESP-NOW channel");
    if (mode == SERVO_RADIO_FOLLOW_PEER) {
        follower.current_channel = channel;
        follower.candidate = next_channel(channel);
        follower.last_activity = xTaskGetTickCount();
        ESP_LOGI(TAG, "Looking for paired Sugarat on channels %u-%u, starting on %u",
                 follower.first_channel, follower.last_channel, channel);
    }
    return start_esp_now(peer_mac);
}

void servo_radio_scan_step(void)
{
    const TickType_t now = xTaskGetTickCount();
    const TickType_t dwell = pdMS_TO_TICKS(follower.locked ? FOLLOW_TIMEOUT_MS : SEARCH_DWELL_MS);
    if (follower.first_channel == 0 || now - follower.last_activity < dwell) {
        return;
    }
    if (follower.locked) {
        ESP_LOGW(TAG, "Sugarat timed out on channel %u; searching again", follower.current_channel);
        follower.locked = false;
    }
    const uint8_t channel = follower.candidate;
    follower.candidate = next_channel(channel);
    follower.last_activity = now;
    const esp_err_t result = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    if (result == ESP_OK) {
        follower.current_channel = channel;
    } else {
        ESP_LOGW(TAG, "Could not tune to channel %u: %s", channel, esp_err_to_name(result));
    }
}

static esp_err_t home_channel(uint8_t *channel)
{
    if (follower.current_channel != 0) {
        *channel = follower.current_channel;
        return ESP_OK;
    }
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        *channel = ap.primary;
        return ESP_OK;
    }
    wifi_second_chan_t secondary;
    return esp_wifi_get_channel(channel, &secondary);
}

bool servo_radio_receive(servo_message_t *message)
{
    while (xQueueReceive(inbox, message, 0) == pdTRUE) {
        uint8_t channel;
        // Adjacent channels can overlap at short distances. Check the peer's advertised channel.
        if (home_channel(&channel) != ESP_OK || message->channel != channel) {
            continue;
        }
        if (follower.first_channel != 0) {
            if (!servo_message_is_command(message->type)) {
                continue;
            }
            follower.last_activity = xTaskGetTickCount();
            if (!follower.locked) {
                follower.locked = true;
                ESP_LOGI(TAG, "Paired Sugarat found on channel %u", channel);
            }
        }
        return true;
    }
    return false;
}

esp_err_t servo_radio_send(servo_message_t message)
{
    message.protocol = PROTOCOL_ID;
    const esp_err_t err = home_channel(&message.channel);
    if (err != ESP_OK) {
        return err;
    }
    return esp_now_send(peer_address, (const uint8_t *)&message, sizeof(message));
}
