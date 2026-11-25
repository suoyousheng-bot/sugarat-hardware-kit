/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/** Start the Sugarat Armor BLE peripheral. */
esp_err_t armor_ble_start(void);

/** Return whether a browser has subscribed to control notifications. */
bool armor_ble_subscribed(void);

/** Send normalized axes, held boost and the cumulative view-button count. */
void armor_ble_send(bool ready, bool boost, float turn, float climb, uint16_t view_count);
