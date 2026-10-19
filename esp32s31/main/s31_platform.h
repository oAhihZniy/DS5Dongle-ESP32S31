#pragma once
// Platform boundary for the upstream Pico code: FreeRTOS tasks, queues and time.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_mac.h"

#define __not_in_flash_func(name) name
using uint = unsigned;
using critical_section_t = SemaphoreHandle_t;
inline void critical_section_init(critical_section_t *s) { *s = xSemaphoreCreateMutex(); configASSERT(*s); }
inline void critical_section_enter_blocking(critical_section_t *s) { xSemaphoreTake(*s, portMAX_DELAY); }
inline void critical_section_exit(critical_section_t *s) { xSemaphoreGive(*s); }
using absolute_time_t = int64_t;
inline absolute_time_t get_absolute_time() { return esp_timer_get_time(); }
inline uint64_t time_us_64() { return esp_timer_get_time(); }
inline uint32_t to_ms_since_boot(absolute_time_t t) { return t / 1000; }
inline int64_t absolute_time_diff_us(absolute_time_t a, absolute_time_t b) { return b - a; }
inline void sleep_ms(unsigned ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }
inline void flash_safe_execute_core_init() {}
struct queue_t { QueueHandle_t handle; };
inline void queue_init(queue_t *q, unsigned size, unsigned count) {
    q->handle = xQueueCreate(count, size); configASSERT(q->handle);
}
inline bool queue_try_add(queue_t *q, const void *p) { return xQueueSend(q->handle, p, 0) == pdTRUE; }
inline bool queue_try_remove(queue_t *q, void *p) {
    // Pico permits dropping an item with NULL. FreeRTOS requires storage.
    alignas(8) uint8_t discard[4096];
    return xQueueReceive(q->handle, p ? p : discard, 0) == pdTRUE;
}
inline bool queue_is_full(queue_t *q) { return uxQueueSpacesAvailable(q->handle) == 0; }
inline unsigned queue_get_level(queue_t *q) { return uxQueueMessagesWaiting(q->handle); }
inline bool queue_is_empty(queue_t *q) { return queue_get_level(q) == 0; }
inline void multicore_launch_core1_with_stack(void (*fn)(), uint32_t *, size_t bytes) {
    auto wrapper = [](void *arg) { reinterpret_cast<void (*)()>(arg)(); vTaskDelete(nullptr); };
    configASSERT(xTaskCreatePinnedToCore(wrapper, "ds5_codec", bytes, reinterpret_cast<void *>(fn), 5, nullptr, 1) == pdPASS);
}
inline unsigned board_usb_get_serial(uint16_t *out, unsigned max) {
    uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_BT);
    const char hex[] = "0123456789ABCDEF";
    unsigned n = max < 12 ? max : 12;
    for (unsigned i=0; i<n; ++i) out[i] = hex[(mac[i/2] >> ((i%2) ? 0 : 4)) & 15];
    return n;
}
