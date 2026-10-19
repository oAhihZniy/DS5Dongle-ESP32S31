#include "ds5_bridge.h"

#include <string.h>

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_hidh_api.h"
#include "esp_log.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "ds5_bt";
static ds5_event_cb_t s_callback;
static void *s_ctx;
static bool s_scanning;
static SemaphoreHandle_t s_init_done;
static esp_hidh_status_t s_init_status = ESP_HIDH_ERR;

static void emit(ds5_event_t event, const uint8_t *data, size_t len)
{
    if (s_callback) s_callback(event, data, len, s_ctx);
}

static void gap_callback(esp_bt_gap_cb_event_t event,
                         esp_bt_gap_cb_param_t *param)
{
    if (!param) return;

    switch (event) {
    case ESP_BT_GAP_DISC_RES_EVT: {
        bool gamepad = false;
        for (int i = 0; i < param->disc_res.num_prop; ++i) {
            const esp_bt_gap_dev_prop_t *prop = &param->disc_res.prop[i];
            if (prop->type == ESP_BT_GAP_DEV_PROP_COD && prop->val &&
                prop->len >= (int)sizeof(uint32_t)) {
                uint32_t cod;
                memcpy(&cod, prop->val, sizeof(cod));
                gamepad = ((cod >> 8) & 0x1fU) == 0x05U;
            }
        }
        if (gamepad) emit(DS5_EVENT_DEVICE_FOUND, param->disc_res.bda,
                          sizeof(esp_bd_addr_t));
        break;
    }
    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
        if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED)
            s_scanning = false;
        break;
    case ESP_BT_GAP_AUTH_CMPL_EVT:
        ESP_LOGI(TAG, "Pairing %s", param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS ?
                 "complete" : "failed");
        break;
    case ESP_BT_GAP_PIN_REQ_EVT: {
        esp_bt_pin_code_t pin = {0};
        esp_bt_gap_pin_reply(param->pin_req.bda, true, 0, pin);
        break;
    }
    case ESP_BT_GAP_CFM_REQ_EVT:
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;
    case ESP_BT_GAP_READ_ACL_REAL_RSSI_EVT:
        if (param->read_acl_real_rssi.stat == ESP_BT_STATUS_SUCCESS)
            emit(DS5_EVENT_RSSI, (const uint8_t *)&param->read_acl_real_rssi.rssi, 1);
        break;
    default:
        break;
    }
}

static void hidh_callback(esp_hidh_cb_event_t event, esp_hidh_cb_param_t *param)
{
    if (!param) return;
    switch (event) {
    case ESP_HIDH_OPEN_EVT:
        if (param->open.status == ESP_HIDH_OK) {
            ESP_LOGI(TAG, "DualSense connected");
            emit(DS5_EVENT_CONNECTED, param->open.bd_addr,
                 sizeof(esp_bd_addr_t));
        } else {
            ESP_LOGW(TAG, "HID open failed: %d", param->open.status);
            emit(DS5_EVENT_DISCONNECTED, NULL, 0);
        }
        break;
    case ESP_HIDH_INIT_EVT:
        s_init_status = param->init.status;
        if (s_init_done) xSemaphoreGive(s_init_done);
        break;
    case ESP_HIDH_CLOSE_EVT:
        ESP_LOGI(TAG, "DualSense disconnected");
        emit(DS5_EVENT_DISCONNECTED, NULL, 0);
        break;
    case ESP_HIDH_DATA_IND_EVT:
        if (param->data_ind.status == ESP_HIDH_OK && param->data_ind.data &&
            param->data_ind.len)
            emit(DS5_EVENT_INPUT, param->data_ind.data, param->data_ind.len);
        break;
    case ESP_HIDH_GET_RPT_EVT:
        if (param->get_rpt.status == ESP_HIDH_OK && param->get_rpt.data &&
            param->get_rpt.len)
            emit(DS5_EVENT_FEATURE, param->get_rpt.data, param->get_rpt.len);
        else emit(DS5_EVENT_FEATURE_ERROR, NULL, 0);
        break;
    default:
        break;
    }
}

esp_err_t ds5_bridge_disconnect(const esp_bd_addr_t address)
{
    return esp_bt_hid_host_disconnect((uint8_t *)address);
}

esp_err_t ds5_bridge_read_rssi(const esp_bd_addr_t address)
{
    return esp_bt_gap_read_acl_real_rssi((uint8_t *)address);
}

esp_err_t ds5_bridge_cancel_scan(void)
{
    s_scanning = false;
    return esp_bt_gap_cancel_discovery();
}

esp_err_t ds5_bridge_start(ds5_event_cb_t callback, void *ctx)
{
    s_callback = callback;
    s_ctx = ctx;
    if (!s_init_done) s_init_done = xSemaphoreCreateBinary();
    if (!s_init_done) return ESP_ERR_NO_MEM;
    (void)xSemaphoreTake(s_init_done, 0);
    esp_err_t err = esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    esp_bt_controller_config_t config = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    err = esp_bt_controller_init(&config);
    if (err == ESP_OK) err = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    if (err == ESP_OK) err = esp_bluedroid_init();
    if (err == ESP_OK) err = esp_bluedroid_enable();
    if (err == ESP_OK) err = esp_bt_gap_register_callback(gap_callback);
    if (err == ESP_OK) err = esp_bt_hid_host_register_callback(hidh_callback);
    if (err == ESP_OK) err = esp_bt_hid_host_init();
    if (err == ESP_OK && xSemaphoreTake(s_init_done, pdMS_TO_TICKS(5000)) != pdTRUE)
        err = ESP_ERR_TIMEOUT;
    if (err == ESP_OK && s_init_status != ESP_HIDH_OK) err = ESP_FAIL;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth HID host init failed: %s", esp_err_to_name(err));
        return err;
    }

    const uint8_t iocap = ESP_BT_IO_CAP_NONE;
    err = esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE,
                                         (void *)&iocap, sizeof(iocap));
    if (err == ESP_OK) err = esp_bt_gap_set_device_name("DS5 Dongle S31");
    if (err == ESP_OK)
        err = esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE,
                                       ESP_BT_GENERAL_DISCOVERABLE);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Stored Bluetooth bonds: %d", esp_bt_gap_get_bond_device_num());
    }
    return err;
}

esp_err_t ds5_bridge_scan(void)
{
    if (s_scanning) return ESP_OK;
    esp_err_t err = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY,
                                               8, 0);
    if (err == ESP_OK) s_scanning = true;
    return err;
}

esp_err_t ds5_bridge_get_bonded_address(esp_bd_addr_t address)
{
    if (!address) return ESP_ERR_INVALID_ARG;
    int bond_count = esp_bt_gap_get_bond_device_num();
    if (bond_count <= 0) return ESP_ERR_NOT_FOUND;

    esp_bd_addr_t bonded[16];
    if (bond_count > (int)(sizeof(bonded) / sizeof(bonded[0])))
        bond_count = sizeof(bonded) / sizeof(bonded[0]);
    esp_err_t err = esp_bt_gap_get_bond_device_list(&bond_count, bonded);
    if (err != ESP_OK || bond_count <= 0) return err == ESP_OK ? ESP_ERR_NOT_FOUND : err;
    nvs_handle_t handle;
    esp_bd_addr_t preferred;
    if (nvs_open("ds5bond", NVS_READONLY, &handle) == ESP_OK) {
        size_t preferred_len = sizeof(preferred);
        if (nvs_get_blob(handle, "address", preferred, &preferred_len) == ESP_OK &&
            preferred_len == sizeof(preferred)) {
            for (int i = 0; i < bond_count; ++i) {
                if (memcmp(preferred, bonded[i], sizeof(preferred)) == 0) {
                    memcpy(address, preferred, sizeof(preferred));
                    nvs_close(handle);
                    ESP_LOGI(TAG, "Reconnecting to the last controller bond");
                    return ESP_OK;
                }
            }
        }
        nvs_close(handle);
    }
    memcpy(address, bonded[0], sizeof(esp_bd_addr_t));
    ESP_LOGI(TAG, "Reconnecting to stored controller bond");
    return ESP_OK;
}

esp_err_t ds5_bridge_connect(const esp_bd_addr_t address)
{
    if (s_scanning) {
        (void)esp_bt_gap_cancel_discovery();
        s_scanning = false;
    }
    return esp_bt_hid_host_connect((uint8_t *)address);
}

esp_err_t ds5_bridge_send_output(const esp_bd_addr_t address,
                                 const uint8_t *data, size_t len)
{
    if (!data || len == 0) return ESP_ERR_INVALID_ARG;
    return esp_bt_hid_host_send_data((uint8_t *)address, (uint8_t *)data, len);
}

esp_err_t ds5_bridge_set_feature(const esp_bd_addr_t address,
                                 const uint8_t *data, size_t len)
{
    if (!data || len == 0) return ESP_ERR_INVALID_ARG;
    return esp_bt_hid_host_set_report((uint8_t *)address,
                                      ESP_HIDH_REPORT_TYPE_FEATURE,
                                      (uint8_t *)data, len);
}

esp_err_t ds5_bridge_get_feature(const esp_bd_addr_t address,
                                 uint8_t report_id, size_t len)
{
    return esp_bt_hid_host_get_report((uint8_t *)address,
                                      ESP_HIDH_REPORT_TYPE_FEATURE,
                                      report_id, (int)len);
}
