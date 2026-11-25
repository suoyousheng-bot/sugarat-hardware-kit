/* SPDX-License-Identifier: Apache-2.0 */
#include "armor_ble.h"

#include <stdatomic.h>
#include <stdint.h>

#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "sdgoods_nvs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *const TAG = "armor_ble";
static const char NAME[] = "Sugarat Armor";
/* NimBLE uses little-endian UUID bytes. Both IDs share the first 15 bytes. */
static const ble_uuid128_t SERVICE = BLE_UUID128_INIT(
    0x20,0x91,0x46,0xc5,0x38,0xdc,0x01,0x8a,0x44,0x41,0xc2,0x6d,0x01,0x40,0x5a,0x73);
static const ble_uuid128_t CONTROL = BLE_UUID128_INIT(
    0x20,0x91,0x46,0xc5,0x38,0xdc,0x01,0x8a,0x44,0x41,0xc2,0x6d,0x02,0x40,0x5a,0x73);

static uint16_t s_control_handle;
static uint8_t s_address_type;
static atomic_uint s_subscriber = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_sequence;

static int control_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    return BLE_ATT_ERR_READ_NOT_PERMITTED;
}

static const struct ble_gatt_svc_def SERVICES[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &SERVICE.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &CONTROL.u,
                .access_cb = control_access,
                .val_handle = &s_control_handle,
                .flags = BLE_GATT_CHR_F_NOTIFY,
            },
            {0},
        },
    },
    {0},
};

static void advertise(void);

/* Asks the computer for a 15-30 ms connection interval (within Apple's accessory limits) so each
 * 20 ms control packet goes out on the next connection event; the computer may grant a longer one. */
static void request_fast_interval(uint16_t conn)
{
    const struct ble_gap_upd_params params = {
        .itvl_min = 12,              /* 15 ms in 1.25 ms units */
        .itvl_max = 24,              /* 30 ms */
        .latency = 0,
        .supervision_timeout = 400,  /* 4 s in 10 ms units */
    };
    const int rc = ble_gap_update_params(conn, &params);
    if (rc != 0) ESP_LOGW(TAG, "Connection interval request failed: %d", rc);
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status != 0) advertise();
        else request_fast_interval(event->connect.conn_handle);
        break;
    case BLE_GAP_EVENT_CONN_UPDATE: {
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(event->conn_update.conn_handle, &desc) == 0) {
            ESP_LOGI(TAG, "Connection interval %u.%02u ms", desc.conn_itvl * 125 / 100, desc.conn_itvl * 125 % 100);
        }
        break;
    }
    case BLE_GAP_EVENT_DISCONNECT:
        atomic_store(&s_subscriber, BLE_HS_CONN_HANDLE_NONE);
        advertise();
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_control_handle) {
            atomic_store(&s_subscriber, event->subscribe.cur_notify
                ? event->subscribe.conn_handle : BLE_HS_CONN_HANDLE_NONE);
        }
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        break;
    default:
        break;
    }
    return 0;
}

static void advertise(void)
{
    const struct ble_hs_adv_fields fields = {
        .flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP,
        .uuids128 = (ble_uuid128_t[]) {SERVICE},
        .num_uuids128 = 1,
        .uuids128_is_complete = true,
    };
    const struct ble_hs_adv_fields scan = {
        .name = (const uint8_t *)NAME,
        .name_len = sizeof(NAME) - 1,
        .name_is_complete = true,
    };
    struct ble_gap_adv_params params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
    };
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc == 0) rc = ble_gap_adv_rsp_set_fields(&scan);
    if (rc == 0) rc = ble_gap_adv_start(s_address_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc != 0) ESP_LOGE(TAG, "Advertising failed: %d", rc);
}

static void on_reset(int reason)
{
    atomic_store(&s_subscriber, BLE_HS_CONN_HANDLE_NONE);
    ESP_LOGW(TAG, "BLE host reset: %d", reason);
}

static void on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &s_address_type);
    if (rc == 0) advertise();
    else ESP_LOGE(TAG, "Cannot select BLE address: %d", rc);
}

static void host_task(void *arg)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t armor_ble_start(void)
{
    sdgoods_nvs_ensure();
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) return err;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    int rc = ble_svc_gap_device_name_set(NAME);
    if (rc == 0) rc = ble_gatts_count_cfg(SERVICES);
    if (rc == 0) rc = ble_gatts_add_svcs(SERVICES);
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT setup failed: %d", rc);
        nimble_port_deinit();
        return ESP_FAIL;
    }
    nimble_port_freertos_init(host_task);
    return ESP_OK;
}

bool armor_ble_subscribed(void)
{
    return atomic_load(&s_subscriber) != BLE_HS_CONN_HANDLE_NONE;
}

static void put_u16(uint8_t *out, uint16_t value)
{
    out[0] = value & 0xff;
    out[1] = value >> 8;
}

void armor_ble_send(bool ready, bool boost, float turn, float climb, uint16_t view_count)
{
    const unsigned conn = atomic_load(&s_subscriber);
    if (conn == BLE_HS_CONN_HANDLE_NONE) return;

    uint8_t packet[10] = {1, (ready ? 1 : 0) | (ready && boost ? 2 : 0)};
    put_u16(packet + 2, s_sequence++);
    put_u16(packet + 4, ready ? (int16_t)(turn * 1000) : 0);
    put_u16(packet + 6, ready ? (int16_t)(climb * 1000) : 0);
    put_u16(packet + 8, view_count);
    struct os_mbuf *message = ble_hs_mbuf_from_flat(packet, sizeof(packet));
    if (message) {
        int rc = ble_gatts_notify_custom(conn, s_control_handle, message);
        if (rc != 0) ESP_LOGD(TAG, "Control notification skipped: %d", rc);
    }
}
