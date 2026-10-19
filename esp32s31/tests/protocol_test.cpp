#include <cassert>
#include <cstdio>
#include <vector>
#include <array>
#include <unordered_map>
#include <algorithm>
#include "tusb.h"
#include "config.h"
#include "utils.h"
#include "dse.h"

uint64_t host_time_us=1000000;
static Config_body config{};
bool is_dse=false;
Config_body &get_config() {return config;}
static tusb_speed_t host_speed=TUSB_SPEED_FULL;
extern "C" tusb_speed_t tud_speed_get() {return host_speed;}
extern "C" bool tud_control_xfer(uint8_t,const tusb_control_request_t *,void *,uint16_t) {return true;}
std::unordered_map<uint8_t,std::vector<uint8_t>> feature_data;
struct Request {uint8_t id;bool write;};
static std::vector<Request> requests;
uint16_t bt_control_cid() {return 1;}
void bt_control_send(const uint8_t *,uint16_t) {}
void set_feature_data(uint8_t id,uint8_t *,uint16_t) {requests.push_back({id,true});}
std::vector<uint8_t> get_feature_data(uint8_t id,uint16_t) {requests.push_back({id,false});return {};}

#include "../main/upstream/usb_descriptors.cpp"
#include "../main/usb_speed_s31.cpp"
#include "../main/upstream/state_mgr.cpp"
#include "../main/upstream/dse.cpp"

static void descriptors_test() {
    for(auto speed:{TUSB_SPEED_FULL,TUSB_SPEED_HIGH})
    for(unsigned mode=0;mode<3;++mode)
    for(bool edge:{false,true})
    for(bool wake:{false,true})
    for(bool shortcuts:{false,true}) {
        host_speed=speed;is_dse=edge;config.controller_mode=2;
        config.enable_wake=wake;config.ps_shortcut_enabled=shortcuts;
        config.polling_rate_mode=mode;
        auto d=reinterpret_cast<const tusb_desc_device_t *>(tud_descriptor_device_cb());
        assert(d->idVendor==0x054c && d->idProduct==(edge?0x0df2:0x0ce6));
        auto c=tud_descriptor_configuration_cb(0);
        unsigned len=c[2]|(c[3]<<8);assert(len==(wake||shortcuts?252:227));
        assert(c[4]==(wake||shortcuts?5:4));
        assert(bool(c[7]&0x20)==wake);
        unsigned p=0,endpoints=0;
        while(p<len) {
            unsigned n=c[p];assert(n>=2 && p+n<=len);
            if(c[p+1]==TUSB_DESC_ENDPOINT) {
                ++endpoints;
                unsigned type=c[p+3]&3;
                if(type==1) assert(c[p+6]==(speed==TUSB_SPEED_HIGH?4:1));
                if(c[p+2]==0x85) assert(c[p+6]==(speed==TUSB_SPEED_HIGH?7:10));
                if(type==3 && (c[p+2]==0x84 || c[p+2]==3))
                    assert(c[p+6]==(speed==TUSB_SPEED_HIGH?(mode==0?6:mode==1?5:4):(mode==0?4:mode==1?2:1)));
            }
            p+=n;
        }
        assert(endpoints==(wake||shortcuts?5:4));
        auto o=tud_descriptor_other_speed_configuration_cb(0);
        assert(o && o[1]==TUSB_DESC_OTHER_SPEED_CONFIG);
        for(unsigned p=0;p<len;p+=o[p])
            if(o[p+1]==TUSB_DESC_ENDPOINT && o[p+2]==0x85)
                assert(o[p+6]==(speed==TUSB_SPEED_HIGH?10:7));
        assert(tud_descriptor_device_qualifier_cb()[0]==10);
    }
    assert(sizeof(desc_hid_report_ds)==321);
    assert(sizeof(desc_hid_report_dse)==437);
    config.enable_usb_sn=0;assert(tud_descriptor_device_cb()[16]==0);
    config.enable_usb_sn=1;assert(tud_descriptor_device_cb()[16]==3);
    config.controller_mode=0;is_dse=true;
    assert(reinterpret_cast<const tusb_desc_device_t *>(tud_descriptor_device_cb())->idProduct==0x0ce6);
    config.controller_mode=1;is_dse=false;
    assert(reinterpret_cast<const tusb_desc_device_t *>(tud_descriptor_device_cb())->idProduct==0x0df2);
}
static void state_test() {
    static_assert(sizeof(SetStateData)==63);
    config={};config.speaker_volume=80;config.headset_volume=70;
    state_init();
    SetStateData update{};update.AllowLedColor=1;update.LedRed=12;update.LedGreen=34;update.LedBlue=56;
    update.AllowLeftTriggerFFB=1;memset(update.LeftTriggerFFB,0x26,11);
    state_update(reinterpret_cast<uint8_t *>(&update),sizeof(update));
    SetStateData actual{};state_set(reinterpret_cast<uint8_t *>(&actual),sizeof(actual));
    assert(actual.LedRed==12 && actual.VolumeSpeaker==80 && actual.LeftTriggerFFB[10]==0x26);
    update={};update.AllowSpeakerVolume=1;update.VolumeSpeaker=40;
    config.lock_volume=1;state_update(reinterpret_cast<uint8_t *>(&update),sizeof(update));
    assert(get_config().speaker_volume==80);
    config.lock_volume=0;state_update(reinterpret_cast<uint8_t *>(&update),sizeof(update));
    assert(get_config().speaker_volume==40);
    update={};update.UseRumbleNotHaptics2=1;update.RumbleEmulationLeft=100;
    state_update(reinterpret_cast<uint8_t *>(&update),sizeof(update));
    state_set(reinterpret_cast<uint8_t *>(&actual),sizeof(actual));assert(actual.RumbleEmulationLeft==100);
}
static void edge_test() {
    requests.clear();host_time_us=1000000;dse_on_connect();
    assert(!dse_profiles_ready());assert(requests.size()==1 && requests[0].id==0x80 && requests[0].write);
    host_time_us+=3999000;dse_task();assert(requests.size()==1);
    host_time_us+=1000;dse_task();
    for(unsigned i=0;i<12;++i) {host_time_us+=80000;dse_task();}
    assert(dse_profiles_ready());assert(requests.size()==13);
    for(unsigned i=0;i<12;++i) assert(requests[i+1].id==0x70+i && !requests[i+1].write);
    dse_on_profile_write(0x60);host_time_us+=500000;dse_task();assert(requests.back().id==0x80);
    for(unsigned i=0;i<6;++i) {host_time_us+=i==0?500000:250000;dse_task();assert(requests.back().id==0x81);}
    host_time_us+=4000000;dse_task();
    for(unsigned i=0;i<12;++i) {host_time_us+=80000;dse_task();}
    assert(requests.back().id==0x7b);
}
int main() {
    descriptors_test();state_test();edge_test();
    puts("PASS: 48 descriptor combinations, USB speeds, state flags, rumble and Edge timing");
}
