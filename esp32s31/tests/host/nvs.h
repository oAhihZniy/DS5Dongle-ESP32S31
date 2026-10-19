#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
using esp_err_t=int;
using nvs_handle_t=int;
constexpr int ESP_OK=0,NVS_READONLY=0,NVS_READWRITE=1;
inline std::vector<uint8_t> host_nvs_blob;
inline esp_err_t nvs_open(const char *,int,nvs_handle_t *h) {*h=1;return 0;}
inline esp_err_t nvs_get_blob(int,const char *,void *data,size_t *len) {
    if(host_nvs_blob.empty() || *len<host_nvs_blob.size()) return 1;
    memcpy(data,host_nvs_blob.data(),host_nvs_blob.size());*len=host_nvs_blob.size();return 0;
}
inline esp_err_t nvs_set_blob(int,const char *,const void *data,size_t len) {
    auto p=static_cast<const uint8_t *>(data);host_nvs_blob.assign(p,p+len);return 0;
}
inline esp_err_t nvs_commit(int) {return 0;}
inline void nvs_close(int) {}
