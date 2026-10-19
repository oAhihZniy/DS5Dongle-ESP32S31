// S31 transport and scheduling; protocol modules in upstream/ follow ca1af99.
#include "s31_platform.h"
#include <algorithm>
#include <unordered_map>
#include <vector>
#include <atomic>
#include "tusb.h"
#include "bt.h"
#include "config.h"
#include "state_mgr.h"
#include "utils.h"
#include "audio.h"
#include "dse.h"
#include "cmd.h"
#include "wake.h"
#include "ps_shortcut.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "esp_gap_bt_api.h"
#include "esp_private/usb_phy.h"
#include "soc/lp_system_reg.h"
#include "soc/soc.h"
#include "nvs_flash.h"
extern "C" {
#include "ds5_bridge.h"
#include "ds5_display.h"
}

static constexpr gpio_num_t BOOT_PIN = GPIO_NUM_61;
static constexpr unsigned EVENT_MAX = 672;
struct Event { ds5_event_t type; uint16_t len; uint8_t data[EVENT_MAX]; };
static QueueHandle_t events;
static QueueHandle_t input_events;
static bool connected, connecting, model_known, scan_enabled;
static bool usb_attached;
static esp_bd_addr_t address;
static uint64_t scan_until, next_scan, connect_started, last_active, probe_started;
static int8_t signal_dbm;
static uint64_t last_rssi;
static uint8_t seq;
static bool spk_active_internal;
bool spk_active = false;
uint8_t interrupt_in_data[63];
std::unordered_map<uint8_t, std::vector<uint8_t>> feature_data;
static std::vector<std::array<uint8_t, 6>> blacklist;
static esp_bd_addr_t explicit_pair;
static bool explicit_pair_valid;
static bool battery_valid, battery_charging;
static bool input_dirty;
static uint64_t last_controller_report;
static uint8_t battery_percent;
static portMUX_TYPE screen_lock = portMUX_INITIALIZER_UNLOCKED;
static std::atomic<uint32_t> lost_events;

static void post_event(ds5_event_t type, const uint8_t *data, size_t len, void *) {
    if (len > EVENT_MAX) return;
    Event e{}; e.type = type; e.len = len;
    if (data && len) memcpy(e.data, data, len);
    // Never block the Bluetooth stack on display, codecs, NVS or USB traffic.
    QueueHandle_t queue=type==DS5_EVENT_INPUT?input_events:events;
    if (xQueueSend(queue, &e, 0) != pdTRUE) ++lost_events;
}

static void blacklist_save() {
    nvs_handle_t h;
    if (nvs_open("ds5bond",NVS_READWRITE,&h)!=ESP_OK) return;
    if (blacklist.empty()) nvs_erase_key(h,"blacklist");
    else nvs_set_blob(h,"blacklist",blacklist.data(),blacklist.size()*6);
    nvs_commit(h); nvs_close(h);
}
static bool blocked(const uint8_t *a) {
    return std::any_of(blacklist.begin(),blacklist.end(),[&](const auto &b){return memcmp(a,b.data(),6)==0;});
}
static void scan_start() {
    scan_enabled=true; scan_until=time_us_64()+30000000; next_scan=0;
}
bool bt_is_connected() { return connected; }
bool bt_disconnect() { return connected && ds5_bridge_disconnect(address)==ESP_OK; }
void bt_power_off_controller() {
    uint8_t data[47]{}; data[0]=2; set_feature_data(0x08,data,sizeof(data));
}
void bt_get_signal_strength(int8_t *rssi) {
    if (rssi) *rssi=signal_dbm;
    if (connected && time_us_64()-last_rssi>=1000000) {
        last_rssi=time_us_64(); ds5_bridge_read_rssi(address);
    }
}
uint16_t bt_control_cid() { return connected ? 1 : 0; }
void bt_control_send(const uint8_t *data,uint16_t len) {
    // Upstream's 0x53 HID SET_REPORT transaction prefix is supplied by Bluedroid.
    if(connected && len>=2 && data[0]==0x53)
        ds5_bridge_set_feature(address,data+1,len-1);
}
void bt_write(const uint8_t *data,uint16_t len) {
    if(!connected || len<4 || len>512) return;
    uint8_t packet[512]; memcpy(packet,data,len);
    fill_output_report_checksum(packet,len);
    ds5_bridge_send_output(address,packet,len);
}
std::vector<uint8_t> get_feature_data(uint8_t id,uint16_t len) {
    auto found=feature_data.find(id);
    if(connected && (found==feature_data.end() || id==0x81 || (id>=0x63 && id<=0x65) || dse_is_profile_report(id)))
        ds5_bridge_get_feature(address,id,len);
    return found==feature_data.end()?std::vector<uint8_t>{}:found->second;
}
void set_feature_data(uint8_t id,uint8_t *data,uint16_t len) {
    if(!connected || len<4 || len>511) return;
    uint8_t packet[512]; packet[0]=id; memcpy(packet+1,data,len);
    fill_feature_report_checksum(packet,len+1);
    ds5_bridge_set_feature(address,packet,len+1);
    dse_on_profile_write(id);
}
void bt_bootsel_click_action() { if(connected) bt_disconnect(); else scan_start(); }
void bt_bootsel_hold_action() {
    if(connected && !blocked(address)) {
        std::array<uint8_t,6> current;memcpy(current.data(),address,6);blacklist.push_back(current);
    }
    int count=esp_bt_gap_get_bond_device_num();
    std::vector<std::array<uint8_t,6>> bonds(count);
    if(count>0 && esp_bt_gap_get_bond_device_list(&count,reinterpret_cast<esp_bd_addr_t *>(bonds.data()))==ESP_OK) {
        for(int i=0;i<count;++i) {
            if(!blocked(bonds[i].data())) blacklist.push_back(bonds[i]);
            esp_bt_gap_remove_bond_device(bonds[i].data());
        }
    }
    blacklist_save(); bt_disconnect(); connecting=false;
    explicit_pair_valid=false; scan_enabled=false; ds5_bridge_cancel_scan();
}

static void attach_usb() {
    if(!usb_attached && !tud_suspended()) { tud_connect(); usb_attached=true; }
}
static void on_event(const Event &e) {
    if(e.type==DS5_EVENT_DEVICE_FOUND && e.len==6 && !connected && !connecting && scan_enabled) {
        memcpy(address,e.data,6); memcpy(explicit_pair,e.data,6); explicit_pair_valid=true;
        connecting=ds5_bridge_connect(address)==ESP_OK; connect_started=time_us_64();
    } else if(e.type==DS5_EVENT_CONNECTED && e.len==6) {
        const bool pairing=explicit_pair_valid && memcmp(explicit_pair,e.data,6)==0;
        if(blocked(e.data) && !pairing) { ds5_bridge_disconnect(e.data); return; }
        memcpy(address,e.data,6); connected=true; connecting=false; scan_enabled=false;
        ds5_bridge_cancel_scan(); model_known=false; feature_data.clear();
        if(pairing && blocked(address)) {
            std::erase_if(blacklist,[&](const auto &b){return memcmp(b.data(),address,6)==0;}); blacklist_save();
        }
        explicit_pair_valid=false;
        nvs_handle_t h;
        if(nvs_open("ds5bond",NVS_READWRITE,&h)==ESP_OK) {
            nvs_set_blob(h,"address",address,6); nvs_commit(h); nvs_close(h);
        }
        state_init(); last_active=probe_started=time_us_64();
        uint8_t init[142]{}; init[0]=0x32; init[1]=0x10; init[2]=0x90; init[3]=0x3f;
        state_set(init+4,sizeof(SetStateData)); bt_write(init,sizeof(init));
        // Pace these requests below rather than overflowing the HID control channel.
        get_feature_data(0x09,20); wake_on_bt_connect();
    } else if(e.type==DS5_EVENT_DISCONNECTED) {
        connected=connecting=false; model_known=false; battery_valid=false;
        xQueueReset(input_events);
        feature_data.clear(); wake_on_bt_disconnect();
        // Match upstream: USB disappears when the controller disconnects.
        if(!tud_suspended()) { tud_disconnect(); usb_attached=false; }
    } else if(e.type==DS5_EVENT_RSSI && e.len==1) {
        signal_dbm=static_cast<int8_t>(e.data[0]);
    } else if(e.type==DS5_EVENT_FEATURE && connected && e.len>=1) {
        const uint8_t id=e.data[0];
        feature_data[id].assign(e.data,e.data+e.len);
        if(id==0x70 && !model_known) {
            is_dse=e.len>1; model_known=true;
            if(is_dse) dse_on_connect();
            attach_usb();
        }
    } else if(e.type==DS5_EVENT_INPUT && connected) {
        const uint8_t *p=e.data; size_t n=e.len;
        // Bluedroid strips the 0xA1 HID transaction prefix; tolerate both formats.
        if(n && p[0]==0xA1) {++p;--n;}
        if(n>=3 && p[0]==0x31 && (p[1]&2)) { mic_add_queue(const_cast<uint8_t *>(p+3),n-3); return; }
        if(n>=65 && p[0]==0x31) {p+=2;n-=2;}
        else if(n>=64 && p[0]==0x01) {++p;--n;}
        else return;
        memcpy(interrupt_in_data,p,63);
        input_dirty=true;
        last_controller_report=time_us_64();
        set_headset(p[53]&1); wake_on_bt_input(p,63); ps_shortcut_tick(p,63);
        uint8_t level=p[52]&15, power=p[52]>>4;
        battery_valid=level<=10 && power<=2;
        if(battery_valid) {battery_percent=power==2?100:level*10; battery_charging=power==1;}
        bool active=p[0]<120 || p[0]>140 || p[1]<120 || p[1]>140 || p[2]<120 || p[2]>140 || p[3]<120 || p[3]>140 || p[4] || p[5] || p[7]!=8 || p[8] || p[9];
        if(active) last_active=time_us_64();
    }
}

uint16_t tud_hid_get_report_cb(uint8_t itf,uint8_t id,hid_report_type_t type,uint8_t *buffer,uint16_t len) {
    if(itf==1) {uint16_t n=std::min<uint16_t>(len,8); memset(buffer,0,n); return n;}
    if(type!=HID_REPORT_TYPE_FEATURE) return 0;
    if(is_pico_cmd(id)) return pico_cmd_get(id,buffer,len);
    if(dse_is_profile_report(id) && !dse_profiles_ready()) {get_feature_data(id,len);return 0;}
    auto data=get_feature_data(id,len);
    if(data.size()<=1) return 0;
    uint16_t n=std::min<size_t>(data.size()-1,len); memcpy(buffer,data.data()+1,n); return n;
}
void tud_hid_set_report_cb(uint8_t itf,uint8_t id,hid_report_type_t type,const uint8_t *data,uint16_t len) {
    if(itf || !data || !len) return;
    if(is_pico_cmd(id)) {pico_cmd_set(id,data,len);return;}
    if(type==HID_REPORT_TYPE_OUTPUT || id==0) {
        if(id==0) {id=*data++;--len;}
        if(id!=2 || len<47) return;
        state_update(data,len);
        if(!((data[0]&2) || (data[38]&8)) && spk_active_internal) return;
        uint8_t packet[78]{}; packet[0]=0x31; packet[1]=seq++<<4; packet[2]=0x10;
        state_set(packet+3,sizeof(SetStateData)); bt_write(packet,sizeof(packet));
    } else if(type==HID_REPORT_TYPE_FEATURE && (id==0x80 || (id>=0x60 && id<=0x62))) {
        set_feature_data(id,const_cast<uint8_t *>(data),len);
    }
}
bool tud_audio_set_itf_cb(uint8_t,const tusb_control_request_t *r) {
    unsigned itf=tu_u16_low(r->wIndex), alt=tu_u16_low(r->wValue);
    if(itf==1) spk_active=spk_active_internal=alt!=0;
    if(itf==2) set_mic_active(alt!=0);
    return true;
}

static void buttons_tick() {
    static uint64_t checked, pressed_at, released_at;
    static bool was_pressed, held;
    static unsigned clicks;
    uint64_t now=time_us_64(); if(now-checked<10000) return; checked=now;
    bool pressed=gpio_get_level(BOOT_PIN)==0;
    if(pressed && !was_pressed) {pressed_at=now;held=false;}
    if(pressed && !held && now-pressed_at>=1500000) {bt_bootsel_hold_action();held=true;clicks=0;}
    if(!pressed && was_pressed && !held && now-pressed_at>=30000) {++clicks;released_at=now;}
    was_pressed=pressed;
    if(!pressed && clicks && now-released_at>=500000) {
        unsigned n=clicks;clicks=0;
        if(n==1) bt_bootsel_click_action();
        else {
            if(n>=3) REG_SET_BIT(LP_SYSTEM_REG_SYS_CTRL_REG,LP_SYSTEM_REG_FORCE_DOWNLOAD_BOOT);
            esp_restart();
        }
    }
}
struct ScreenState {bool pc, controller, valid, charging, low; uint8_t percent;};
static ScreenState screen_state;
static void display_task(void *) {
    if(ds5_display_start()!=ESP_OK) {vTaskDelete(nullptr);return;}
    while(true) {
        portENTER_CRITICAL(&screen_lock); auto s=screen_state; portEXIT_CRITICAL(&screen_lock);
        ds5_display_update(s.pc,s.controller,s.valid,s.percent,s.charging);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

extern "C" void app_main() {
    esp_err_t e=nvs_flash_init();
    if(e==ESP_ERR_NVS_NO_FREE_PAGES || e==ESP_ERR_NVS_NEW_VERSION_FOUND) {ESP_ERROR_CHECK(nvs_flash_erase());e=nvs_flash_init();}
    ESP_ERROR_CHECK(e); config_load(); state_init();wake_init();
    memset(interrupt_in_data,0,63); memset(interrupt_in_data,128,4); interrupt_in_data[7]=8;
    nvs_handle_t h;
    if(nvs_open("ds5bond",NVS_READONLY,&h)==ESP_OK) {
        size_t n=0;
        if(nvs_get_blob(h,"blacklist",nullptr,&n)==ESP_OK && n%6==0 && n<=96) {
            blacklist.resize(n/6);nvs_get_blob(h,"blacklist",blacklist.data(),&n);
        }
        nvs_close(h);
    }
    events=xQueueCreate(32,sizeof(Event));configASSERT(events);
    input_events=xQueueCreate(32,sizeof(Event));configASSERT(input_events);
    gpio_config_t button{}; button.pin_bit_mask=1ULL<<BOOT_PIN;button.mode=GPIO_MODE_INPUT;button.pull_up_en=GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK(gpio_config(&button));
    usb_phy_config_t phy{};phy.controller=USB_PHY_CTRL_OTG;phy.target=USB_PHY_TARGET_UTMI;
    phy.otg_mode=USB_OTG_MODE_DEVICE;phy.otg_speed=USB_PHY_SPEED_HIGH;
    usb_phy_handle_t handle;ESP_ERROR_CHECK(usb_new_phy(&phy,&handle));
    tusb_rhport_init_t init{};init.role=TUSB_ROLE_DEVICE;init.speed=TUSB_SPEED_HIGH;
    configASSERT(tusb_init(0,&init));tud_disconnect();
    audio_init();
    configASSERT(xTaskCreate(display_task,"ds5_screen",8192,nullptr,1,nullptr)==pdPASS);
    ESP_ERROR_CHECK(ds5_bridge_start(post_event,nullptr));
    if(ds5_bridge_get_bonded_address(address)==ESP_OK && !blocked(address)) {
        connecting=ds5_bridge_connect(address)==ESP_OK;connect_started=time_us_64();
    }
    scan_start();
    uint64_t last_input=0,last_prefetch=0;unsigned prefetch=0;
    bool prev_connected=false;
    while(true) {
        tud_task_ext(0,false);
        Event event; for(unsigned i=0;i<32 && xQueueReceive(events,&event,0)==pdTRUE;++i) on_event(event);
        for(unsigned i=0;i<32 && xQueueReceive(input_events,&event,0)==pdTRUE;++i) on_event(event);
        uint64_t now=time_us_64();
        if(last_controller_report && now-last_controller_report>2000000) battery_valid=false;
        if(connected && !prev_connected) {prefetch=0;last_prefetch=0;}
        prev_connected=connected;
        if(connecting && now-connect_started>10000000) {ds5_bridge_disconnect(address);connecting=false;}
        if(scan_enabled && !connected && !connecting) {
            if(now>=scan_until) {scan_enabled=false;ds5_bridge_cancel_scan();}
            else if(now>=next_scan) {ds5_bridge_scan();next_scan=now+11000000;}
        }
        static const uint8_t ids[]={0x20,0x22,0x05,0x70};
        if(connected && prefetch<sizeof(ids) && now-last_prefetch>=120000) {
            last_prefetch=now;get_feature_data(ids[prefetch++],64);
        }
        if(connected && !model_known && now-probe_started>=2000000) {is_dse=false;model_known=true;attach_usb();}
        if(connected && get_config().inactive_time && now-last_active>uint64_t(get_config().inactive_time)*60000000) {bt_disconnect();last_active=now;}
        if(connected) bt_get_signal_strength(nullptr);
        dse_task(); wake_task(); audio_loop(); buttons_tick();
        unsigned interval=get_config().polling_rate_mode==0?4000:(get_config().polling_rate_mode==1?2000:1000);
        if(connected && tud_hid_ready() && now-last_input>=interval &&
           (get_config().polling_rate_mode!=2 || input_dirty)) {
            if(tud_hid_report(1,interrupt_in_data,63)) {last_input=now;input_dirty=false;}
        }
        ScreenState s{tud_mounted(),connected,battery_valid,battery_charging,
                      battery_valid && !battery_charging && battery_percent<=10,battery_percent};
        portENTER_CRITICAL(&screen_lock);screen_state=s;portEXIT_CRITICAL(&screen_lock);
        auto lost=lost_events.exchange(0);
        if(lost) printf("[BT] Dropped %lu reports\n",(unsigned long)lost);
        vTaskDelay(1);
    }
}
