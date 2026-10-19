#include <cstring>
#include "tusb.h"

// Upstream Pico only enumerates at full speed. S31's UTMI PHY also needs
// the USB 2.0 qualifier and an alternate-speed configuration descriptor.
extern "C" uint8_t const *tud_descriptor_device_cb();
extern "C" uint8_t const *tud_descriptor_configuration_cb(uint8_t);

extern "C" uint8_t const *tud_descriptor_device_qualifier_cb() {
    static tusb_desc_device_qualifier_t q{};
    auto d=reinterpret_cast<const tusb_desc_device_t *>(tud_descriptor_device_cb());
    q.bLength=sizeof(q);q.bDescriptorType=TUSB_DESC_DEVICE_QUALIFIER;
    q.bcdUSB=0x0200;q.bDeviceClass=d->bDeviceClass;
    q.bDeviceSubClass=d->bDeviceSubClass;q.bDeviceProtocol=d->bDeviceProtocol;
    q.bMaxPacketSize0=64;q.bNumConfigurations=1;
    return reinterpret_cast<const uint8_t *>(&q);
}
extern "C" uint8_t const *tud_descriptor_other_speed_configuration_cb(uint8_t index) {
    static uint8_t other[512];
    const uint8_t *config=tud_descriptor_configuration_cb(index);
    const unsigned len=config[2]|(config[3]<<8);
    if(len>sizeof(other)) return nullptr;
    memcpy(other,config,len);other[1]=TUSB_DESC_OTHER_SPEED_CONFIG;
    for(unsigned p=0;p<len;) {
        unsigned n=other[p];if(!n || p+n>len) return nullptr;
        if(other[p+1]==TUSB_DESC_ENDPOINT) {
            unsigned type=other[p+3]&3;
            if(type==1) other[p+6]=tud_speed_get()==TUSB_SPEED_HIGH?1:4;
            else if(type==3) {
                unsigned v=other[p+6];
                other[p+6]=other[p+2]==0x85 ? (tud_speed_get()==TUSB_SPEED_HIGH ? 10 : 7) :
                    tud_speed_get()==TUSB_SPEED_HIGH ? (v>=4 ? 1U<<(v-4) : 1) :
                    (v==4?6:(v==2?5:4));
            }
        }
        p+=n;
    }
    return other;
}
