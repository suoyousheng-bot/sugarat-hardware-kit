#include "manual_control.h"

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "servo_radio.h"
#ifndef SERVO_MANUAL_ONLY
#include "servo_tool.h"
#endif

enum {
    QUERY_US = 350000,
    CALL_QUERY_US = 900000,
    RETRY_US = 180000,
    LINK_TIMEOUT_US = 2500000,
    RADIO_RETRY_US = 1500000,
    MAX_TOUCH_SENDS = 5,
};

static const char *TAG = "sugarat_remote";

static struct {
    bool wifi_managed;
    bool ready;
    bool visible;
    uint32_t session;
    uint32_t sequence;
    uint32_t last_received_sequence;
    int64_t last_reply;
    int64_t next_init;
    int64_t next_query;
    int64_t next_retry;
    unsigned attempts;
    servo_message_t pending;
#ifndef SERVO_MANUAL_ONLY
    bool pending_tool;
    servo_tool_request_t tool;
#endif
} radio;

static bool create_command(screen_action_t action, manual_control_view_t state, servo_message_t *message)
{
    switch (action.kind) {
    case SCREEN_SET_TARGET:
        if (action.value > 1) {
            return false;
        }
        if (action.value == state.position && (state.light == SERVO_LIGHT_UNKNOWN ||
            state.light == (action.value ? SERVO_LIGHT_ON : SERVO_LIGHT_OFF))) {
            return false;
        }
        message->type = SERVO_SET_POSITION;
        message->position = action.value;
        return true;
    case SCREEN_SET_SPEED:
        if (action.value < SERVO_SPEED_MIN || action.value > SERVO_SPEED_MAX || action.value == state.speed) {
            return false;
        }
        message->type = SERVO_SET_SPEED;
        message->speed = action.value;
        return true;
    case SCREEN_SET_ANGLE:
        if (action.value > state.angle_limit_deg || action.value == state.angle_deg) {
            return false;
        }
        message->type = SERVO_SET_ANGLE;
        message->angle_deg = action.value;
        return true;
    case SCREEN_SET_LIGHT:
        if (state.light == SERVO_LIGHT_UNKNOWN || action.value == state.light ||
            (action.value != SERVO_LIGHT_OFF && action.value != SERVO_LIGHT_ON)) {
            return false;
        }
        message->type = SERVO_SET_LIGHT;
        message->light = action.value;
        return true;
    default:
        return false;
    }
}

void manual_control_init(bool wifi_managed)
{
    radio.wifi_managed = wifi_managed;
    radio.session = esp_random();
    radio.last_reply = -LINK_TIMEOUT_US;
}

static void read_state(manual_control_view_t *view, int64_t now)
{
    servo_message_t reply;
    if (!servo_radio_receive(&reply) || reply.type != SERVO_STATE || reply.session != radio.session ||
        reply.sequence < radio.last_received_sequence || reply.sequence > radio.sequence ||
        reply.position > 1 || reply.speed < SERVO_SPEED_MIN || reply.speed > SERVO_SPEED_MAX || reply.moving > 1 ||
        (reply.angle_limit_deg != SERVO_ANGLE_DEFAULT && reply.angle_limit_deg != SERVO_ANGLE_MAX) ||
        reply.angle_deg > reply.angle_limit_deg) {
        return;
    }

    const bool confirmed = radio.pending.type && radio.pending.sequence == reply.sequence;
    if (confirmed) {
#ifndef SERVO_MANUAL_ONLY
        if (radio.pending_tool) {
            const servo_tool_state_t state = {.target = reply.position, .moving = reply.moving != 0};
            servo_tool_complete(&radio.tool, &state);
            radio.pending_tool = false;
        }
#endif
        radio.pending.type = 0;
        radio.next_query = now;
    }
    radio.last_received_sequence = reply.sequence;
    radio.last_reply = now;
    const uint8_t light = reply.light <= SERVO_LIGHT_ON ? reply.light : SERVO_LIGHT_UNKNOWN;
    if (!view->online || confirmed || view->position != reply.position || view->moving != (reply.moving != 0) ||
        view->speed != reply.speed || view->angle_deg != reply.angle_deg ||
        view->angle_limit_deg != reply.angle_limit_deg || view->receiver_channel != reply.channel || view->light != light) {
        ESP_LOGI(TAG, "C3 confirmed target %c ch %u, speed %u, B angle %u/%u, light %s%s",
                 'A' + reply.position, (unsigned)reply.channel, (unsigned)reply.speed,
                 (unsigned)reply.angle_deg, (unsigned)reply.angle_limit_deg,
                 light == SERVO_LIGHT_ON ? "on" : light == SERVO_LIGHT_OFF ? "off" : "unavailable",
                 reply.moving ? ", moving" : "");
    }
    *view = (manual_control_view_t){
        .position = reply.position, .speed = reply.speed, .angle_deg = reply.angle_deg,
        .angle_limit_deg = reply.angle_limit_deg, .receiver_channel = reply.channel, .light = light,
        .moving = reply.moving != 0,
    };
}

static void send_pending(const int64_t now)
{
    bool timed_out = radio.attempts >= MAX_TOUCH_SENDS;
#ifndef SERVO_MANUAL_ONLY
    if (radio.pending_tool) timed_out = now >= radio.tool.deadline_us;
#endif
    if (!timed_out) {
        const esp_err_t err = servo_radio_send(radio.pending);
#ifndef SERVO_MANUAL_ONLY
        if (radio.pending_tool && err == ESP_OK) servo_tool_radio_queued(&radio.tool);
#endif
        if (radio.attempts == 0 && err != ESP_OK) ESP_LOGW(TAG, "ESP-NOW command send: %s", esp_err_to_name(err));
        ++radio.attempts;
        radio.next_retry = now + RETRY_US;
        return;
    }
    ESP_LOGW(TAG, "C3 did not acknowledge command %u", (unsigned)radio.pending.type);
#ifndef SERVO_MANUAL_ONLY
    if (radio.pending_tool) {
        servo_tool_complete(&radio.tool, NULL);
        radio.pending_tool = false;
    }
#endif
    radio.pending.type = 0;
    radio.next_query = now;
}

void manual_control_update(bool visible, bool call_active, screen_action_t action, manual_control_view_t *view)
{
    const int64_t now = esp_timer_get_time();
    if (visible && !radio.visible) {
        radio.next_query = now;
        radio.last_reply = -LINK_TIMEOUT_US;
        ESP_LOGI(TAG, "Manual controls opened; searching for C3");
    }
    radio.visible = visible;

    bool started = false;
    if (visible && radio.ready && view->online && !radio.pending.type) {
        started = create_command(action, *view, &radio.pending);
    }
#ifndef SERVO_MANUAL_ONLY
    if (!radio.pending.type && servo_tool_receive(&radio.tool)) {
        if (now >= radio.tool.deadline_us) {
            servo_tool_complete(&radio.tool, NULL);
        } else {
            radio.pending = (servo_message_t){
                .type = radio.tool.operation == SERVO_TOOL_GET_STATUS ? SERVO_QUERY : SERVO_SET_POSITION,
                .position = radio.tool.position,
            };
            radio.pending_tool = true;
            started = true;
        }
    }
#endif
    if (started) {
        radio.pending.session = radio.session;
        radio.pending.sequence = ++radio.sequence;
        radio.attempts = 0;
        radio.next_retry = now;
    }

    if (!visible && !radio.pending.type) {
        view->online = false;
        view->pending = false;
        return;
    }
    if (!radio.ready && now >= radio.next_init) {
        radio.next_init = now + RADIO_RETRY_US;
        const esp_err_t err = radio.wifi_managed ? servo_radio_attach(SERVO_PEER_MAC)
                                                : servo_radio_start(SERVO_PEER_MAC, SERVO_RADIO_FIXED);
        radio.ready = err == ESP_OK;
        if (!radio.ready) ESP_LOGW(TAG, "ESP-NOW unavailable: %s", esp_err_to_name(err));
    }
    const bool was_online = view->online;
    if (radio.ready) {
        read_state(view, now);
    }
    view->online = now - radio.last_reply < LINK_TIMEOUT_US;
    if (visible && was_online && !view->online) ESP_LOGW(TAG, "C3 status timed out");
    if (radio.pending.type && now >= radio.next_retry) {
        if (radio.ready) {
            send_pending(now);
#ifndef SERVO_MANUAL_ONLY
        } else if (radio.pending_tool && now >= radio.tool.deadline_us) {
            servo_tool_complete(&radio.tool, NULL);
            radio.pending.type = 0;
            radio.pending_tool = false;
#endif
        }
    }
    if (visible && radio.ready && !radio.pending.type && now >= radio.next_query) {
        (void)servo_radio_send((servo_message_t){.type = SERVO_QUERY, .session = radio.session, .sequence = ++radio.sequence});
        radio.next_query = now + (call_active ? CALL_QUERY_US : QUERY_US);
    }
    view->pending = radio.pending.type != 0;
}
