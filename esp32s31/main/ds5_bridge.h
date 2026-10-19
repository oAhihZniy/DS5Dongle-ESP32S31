#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_bt_defs.h"
#include "esp_err.h"

typedef enum {
    DS5_EVENT_DEVICE_FOUND,
    DS5_EVENT_CONNECTED,
    DS5_EVENT_DISCONNECTED,
    DS5_EVENT_INPUT,
    DS5_EVENT_FEATURE,
    DS5_EVENT_RSSI,
    DS5_EVENT_FEATURE_ERROR,
} ds5_event_t;

typedef void (*ds5_event_cb_t)(ds5_event_t event, const uint8_t *data,
                               size_t len, void *ctx);

esp_err_t ds5_bridge_start(ds5_event_cb_t callback, void *ctx);
esp_err_t ds5_bridge_scan(void);
esp_err_t ds5_bridge_get_bonded_address(esp_bd_addr_t address);
esp_err_t ds5_bridge_connect(const esp_bd_addr_t address);
esp_err_t ds5_bridge_send_output(const esp_bd_addr_t address,
                                 const uint8_t *data, size_t len);
esp_err_t ds5_bridge_set_feature(const esp_bd_addr_t address,
                                 const uint8_t *data, size_t len);
esp_err_t ds5_bridge_get_feature(const esp_bd_addr_t address,
                                 uint8_t report_id, size_t len);
esp_err_t ds5_bridge_disconnect(const esp_bd_addr_t address);
esp_err_t ds5_bridge_read_rssi(const esp_bd_addr_t address);
esp_err_t ds5_bridge_cancel_scan(void);
