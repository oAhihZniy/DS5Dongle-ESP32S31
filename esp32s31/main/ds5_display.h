#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t ds5_display_start(void);
void ds5_display_update(bool pc_connected, bool controller_connected,
                        bool battery_valid, uint8_t battery_percent,
                        bool battery_charging);
