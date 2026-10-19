#include <cassert>
#include "../main/config_s31.cpp"
#include "../main/upstream/state_mgr.cpp"
int main() {
    config_load();
    auto &c=get_config();
    assert(c.config_version==5 && c.haptics_gain==1.0f);
    assert(c.speaker_volume==100 && c.polling_rate_mode==1 && c.controller_mode==2);
    assert(!c.enable_wake && !c.ps_shortcut_enabled && !c.enable_usb_sn);
    c.enable_wake=1;c.ps_shortcut_enabled=1;c.trigger_reduce=8;
    assert(config_save());c.enable_wake=0;config_load();
    assert(c.enable_wake==1 && c.ps_shortcut_enabled==1 && c.trigger_reduce==8);
    Config_body invalid=c;invalid.polling_rate_mode=99;invalid.trigger_reduce=255;
    invalid.haptics_gain=nanf("");invalid.controller_mode=255;
    set_config(invalid);
    assert(c.polling_rate_mode==1 && c.trigger_reduce==0 && c.haptics_gain==1.0f && c.controller_mode==2);
    host_nvs_blob.resize(2);config_load();assert(c.enable_wake==0 && c.speaker_volume==100);
    puts("PASS: configuration defaults, validation, persistence and malformed storage");
}
