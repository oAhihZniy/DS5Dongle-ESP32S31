#include <cassert>
#include <cstdio>
#include <vector>
#include <array>
#include <cstring>
#include "tusb.h"
#include "config.h"
uint64_t host_time_us=1000000;
static Config_body config{};
Config_body &get_config() {return config;}
static unsigned powered_off,wake_requests;
static unsigned forced_wakes;
static bool permission=true;
static std::vector<std::array<uint8_t,8>> keys;
void bt_power_off_controller() {++powered_off;}
extern "C" bool tud_remote_wakeup() {++wake_requests;return permission;}
extern "C" void dcd_remote_wakeup(uint8_t) {++forced_wakes;}
extern "C" bool tud_hid_n_ready(uint8_t) {return true;}
extern "C" bool tud_hid_n_report(uint8_t,uint8_t,const void *report,uint16_t len) {
    assert(len==8);std::array<uint8_t,8> k;memcpy(k.data(),report,8);keys.push_back(k);return true;
}
extern "C" bool tud_hid_n_keyboard_report(uint8_t instance,uint8_t id,uint8_t modifier,const uint8_t keycode[6]) {
    uint8_t report[8]={modifier,0};if(keycode) memcpy(report+2,keycode,6);
    return tud_hid_n_report(instance,id,report,sizeof(report));
}
#include "../main/upstream/ps_shortcut.cpp"
#include "../main/upstream/wake.cpp"
int main() {
    wake_init();
    tud_suspend_cb(false);host_time_us+=2999000;wake_task();assert(powered_off==0);
    tud_resume_cb();host_time_us+=2000000;wake_task();assert(powered_off==0);
    tud_suspend_cb(false);host_time_us+=3000000;wake_task();assert(powered_off==1);
    tud_resume_cb();
    wake_note_usb_reconnect();tud_suspend_cb(false);host_time_us+=3000000;wake_task();assert(powered_off==1);
    tud_mount_cb();
    config.enable_wake=1;permission=false;
    tud_suspend_cb(false);uint8_t input[63]{};input[7]=8;input[9]=1;
    wake_on_bt_input(input,63);assert(wake_requests==1 && keys.empty() && forced_wakes==1);
    tud_resume_cb();host_time_us+=150000;wake_task();assert(keys.back()[2]==0x68);
    host_time_us+=80000;wake_task();assert(keys.back()[2]==0);
    config.ps_shortcut_enabled=1;keys.clear();ps_shortcut_reset();
    host_time_us+=100000;input[9]=1;ps_shortcut_tick(input,63);
    host_time_us+=100000;input[9]=0;ps_shortcut_tick(input,63);
    assert(keys.back()[0]==KEYBOARD_MODIFIER_LEFTGUI && keys.back()[2]==HID_KEY_G);
    host_time_us+=100000;ps_shortcut_tick(input,63);
    input[9]=1;host_time_us+=100000;ps_shortcut_tick(input,63);
    host_time_us+=750000;ps_shortcut_tick(input,63);
    assert(keys.back()[0]==KEYBOARD_MODIFIER_LEFTGUI && keys.back()[2]==HID_KEY_TAB);
    puts("PASS: suspend debounce, reconnect grace, upstream wake fallback, F15 and PS shortcuts");
}
