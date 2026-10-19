#pragma once
#include <cstdint>
#include <cstring>
extern uint64_t host_time_us;
inline uint64_t get_absolute_time() { return host_time_us; }
inline uint32_t to_ms_since_boot(uint64_t t) { return t/1000; }
inline unsigned board_usb_get_serial(uint16_t *out,unsigned) {out[0]='1';return 1;}
inline uint64_t time_us_64() {return host_time_us;}
using critical_section_t=int;
inline void critical_section_init(int *) {}
inline void critical_section_enter_blocking(int *) {}
inline void critical_section_exit(int *) {}
