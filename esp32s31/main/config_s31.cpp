#include "config.h"
#include <cmath>
#include <cstring>
#include <cstdio>
#include "state_mgr.h"
#include "nvs.h"
static constexpr uint32_t CONFIG_MAGIC = 0x66ccff00;
static constexpr uint16_t CONFIG_VERSION = 5;
static Config config{};
bool is_dse = false;
void config_valid() {
    // valid config and set default value
    if (config.magic != CONFIG_MAGIC) {
        config.magic = CONFIG_MAGIC;
        printf("[Config] Config Magic Header is invalid\n");
    }
    if (config.size != sizeof(Config_body)) {
        config.size = sizeof(Config_body);
        printf("[Config] Config Body size is invalid\n");
    }
    auto body = &config.body;
    if (body->config_version != CONFIG_VERSION) {
        memset(body, 0xFF, sizeof(Config_body));
        body->config_version = CONFIG_VERSION;
        printf("[Config] Warning: Config may breaking change. Reset to default\n");
    }
    if (std::isnan(body->haptics_gain) || body->haptics_gain < 1.0f || body->haptics_gain > 2.0f) {
        body->haptics_gain = 1.0f;
        printf("[Config] Haptics Gain value is invalid\n");
    }
    if (body->speaker_volume > 127) {
        body->speaker_volume = 100;
        printf("[Config] Speaker Volume is invalid\n");
    }
    if (body->headset_volume > 127) {
        body->headset_volume = 100;
        printf("[Config] Headset Volume is invalid\n");
    }
    if (body->speaker_gain > 7) {
        body->speaker_gain = 2;
        printf("[Config] speaker_gain is invalid\n");
    }
    if (body->inactive_time > 60) {
        body->inactive_time = 30;
        printf("[Config] Inactive time is invalid\n");
    }
    if (body->disable_pico_led > 1) {
        body->disable_pico_led = 0;
        printf("[Config] disable_pico_led is invalid\n");
    }
    if (body->polling_rate_mode > 2) {
        body->polling_rate_mode = 1;
        printf("[Config] polling_rate_mode is invalid\n");
    }
    if (body->audio_buffer_length < 16 || body->audio_buffer_length > 128) {
        body->audio_buffer_length = 64;
        printf("[Config] haptics_buffer_length is invalid\n");
    }
    if (body->controller_mode > 2) {
        body->controller_mode = 2;
        printf("[Config] controller_mode is invalid\n");
    }
    if (body->enable_usb_sn > 1) {
        body->enable_usb_sn = 0;
        printf("[Config] Warning: enable_usb_sn is invalid\n");
    }
    if (body->ps_shortcut_enabled > 1) {
        body->ps_shortcut_enabled = 0;
        printf("[Config] ps_shortcut_enabled is invalid\n");
    }
    if (body->disable_mic > 1) {
        body->disable_mic = 0;
        printf("[Config] disable_mic is invalid\n");
    }
    if (body->disable_speaker > 1) {
        body->disable_speaker = 0;
        printf("[Config] disable_speaker is invalid\n");
    }
    if (body->enable_wake > 1) {
        body->enable_wake = 0;
        printf("[Config] enable_wake is invalid\n");
    }
    if (body->trigger_reduce > 10) {
        body->trigger_reduce = 0;
        printf("[Config] trigger_reduce is invalid\n");
    }
    if (body->lock_volume > 1) {
        body->lock_volume = 0;
        printf("[Config] lock_volume is invalid\n");
    }
}


void config_load() {
 nvs_handle_t h;
 memset(&config, 0xff, sizeof(config));
 if (nvs_open("ds5config", NVS_READONLY, &h) == ESP_OK) {
   size_t n = sizeof(config.body);
   if (nvs_get_blob(h,"config",&config.body,&n)!=ESP_OK || n!=sizeof(config.body)) memset(&config.body,0xff,sizeof(config.body));
   nvs_close(h);
 }
 config_valid();
}
bool config_save() {
 nvs_handle_t h;
 if (nvs_open("ds5config",NVS_READWRITE,&h)!=ESP_OK) return false;
 esp_err_t e=nvs_set_blob(h,"config",&config.body,sizeof(config.body));
 if(e==ESP_OK) e=nvs_commit(h);
 nvs_close(h); return e==ESP_OK;
}
Config_body &get_config() { return config.body; }
void set_config(const uint8_t *data,uint16_t len) {
 memcpy(&config.body,data,len<sizeof(config.body)?len:sizeof(config.body)); config_valid();
 set_volume(config.body.speaker_volume,config.body.headset_volume);
 if(config.body.speaker_gain) set_gain(config.body.speaker_gain);
 if(config.body.trigger_reduce) set_trigger_reduce(config.body.trigger_reduce);
}
void set_config(const Config_body &body) { set_config(reinterpret_cast<const uint8_t*>(&body),sizeof(body)); }
