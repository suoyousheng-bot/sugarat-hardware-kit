#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t position;
    uint8_t speed;
    uint8_t angle_deg;
    uint8_t angle_limit_deg;
    uint8_t receiver_channel;
    uint8_t light;
    bool online;
    bool pending;
    bool moving;
} manual_control_view_t;

typedef enum {
    SCREEN_NONE,
    SCREEN_SET_SPEED,
    SCREEN_SET_ANGLE,
    SCREEN_SET_TARGET,
    SCREEN_SET_LIGHT,
    SCREEN_START_CALL,
    SCREEN_END_CALL,
    SCREEN_SETUP_WIFI,
    SCREEN_SET_VOLUME,
    SCREEN_SET_BRIGHTNESS,
} screen_action_kind_t;

typedef struct {
    screen_action_kind_t kind;
    uint8_t value;
    bool preview;
} screen_action_t;

void manual_control_init(bool wifi_managed);
void manual_control_update(bool visible, bool call_active, screen_action_t action, manual_control_view_t *view);
