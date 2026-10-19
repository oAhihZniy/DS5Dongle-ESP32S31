#include "ds5_display.h"

#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_efuse.h"
#include "esp_efuse_table.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LCD_WIDTH 480
#define LCD_HEIGHT 480
#define LCD_LINES_PER_CHUNK 16
#define LCD_CS GPIO_NUM_50
#define LCD_DATA0 GPIO_NUM_36
#define LCD_DATA1 GPIO_NUM_51
#define LCD_DATA2 GPIO_NUM_35
#define LCD_DATA3 GPIO_NUM_9

static const char *TAG = "ds5_display";
static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t s_panel;
static uint16_t *s_pixels;
static bool s_started;
static bool s_last_pc;
static bool s_last_controller;
static bool s_last_battery_valid;
static uint8_t s_last_battery_percent;
static bool s_last_battery_charging;
static bool s_have_last_state;
static bool s_last_low_blink;

static const uint8_t s_font[36][7] = {
    {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30}, {14,17,16,16,16,17,14},
    {30,17,17,17,17,17,30}, {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17}, {14,4,4,4,4,4,14},
    {7,2,2,2,18,18,12}, {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17}, {14,17,17,17,17,17,14},
    {30,17,17,30,16,16,16}, {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4}, {17,17,17,17,17,17,14},
    {17,17,17,17,17,10,4}, {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31},
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14}, {14,17,1,2,4,8,31},
    {30,1,1,14,1,1,30}, {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8}, {14,17,17,14,17,17,14},
    {14,17,17,15,1,1,14},
};

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8U) << 8) | ((g & 0xFCU) << 3) | (b >> 3));
}

static const uint8_t *glyph_rows(char c)
{
    if (c >= 'A' && c <= 'Z') return s_font[(unsigned)(c - 'A')];
    if (c >= '0' && c <= '9') return s_font[26U + (unsigned)(c - '0')];
    static const uint8_t colon[7] = {0,4,4,0,4,4,0};
    static const uint8_t dash[7] = {0,0,0,31,0,0,0};
    static const uint8_t plus[7] = {0,4,4,31,4,4,0};
    if (c == ':') return colon;
    if (c == '-') return dash;
    if (c == '+') return plus;
    static const uint8_t percent[7] = {17,1,2,4,8,16,17};
    if (c == '%') return percent;
    return NULL;
}

static bool text_pixel(const char *text, int x0, int y0, int scale,
                       int x, int y)
{
    if (x < x0 || y < y0) return false;
    int cx = (x - x0) / (6 * scale);
    int gx = (x - x0) % (6 * scale) / scale;
    int gy = (y - y0) / scale;
    if (gx >= 5 || gy >= 7 || (size_t)cx >= strlen(text)) return false;
    const uint8_t *rows = glyph_rows(text[cx]);
    return rows && (rows[gy] & (1U << (4 - gx))) != 0;
}

static uint16_t pixel_color(int x, int y, bool pc, bool controller,
                            const char *battery_text, bool low_blink)
{
    const uint16_t background = rgb565(7, 13, 21);
    const uint16_t card = rgb565(18, 30, 41);
    const uint16_t edge = rgb565(45, 69, 81);
    const uint16_t cyan = rgb565(65, 208, 218);
    const uint16_t muted = rgb565(132, 157, 169);
    const uint16_t on = rgb565(65, 215, 130);
    const uint16_t off = rgb565(245, 174, 66);
    uint16_t color = background;

    if ((x >= 76 && x <= 404 && (y == 176 || y == 248 || y == 273 || y == 345)) ||
        ((x == 76 || x == 404) && ((y >= 176 && y <= 248) || (y >= 273 && y <= 345)))) {
        color = edge;
    } else if ((x > 76 && x < 404 && y > 176 && y < 248) ||
               (x > 76 && x < 404 && y > 273 && y < 345)) {
        color = card;
    }
    if (x >= 88 && x <= 392 && y >= 151 && y <= 153) color = cyan;

    int dx = x - 116;
    int dy = y - 212;
    if (dx * dx + dy * dy <= 13 * 13) color = pc ? on : off;
    dx = x - 116;
    dy = y - 309;
    if (dx * dx + dy * dy <= 13 * 13) color = controller ? on : off;

    if (text_pixel("DS5 DONGLE", 150, 79, 3, x, y)) color = cyan;
    if (text_pixel("CONNECTION STATUS", 138, 120, 2, x, y)) color = muted;
    if (text_pixel("USB TO PC", 153, 188, 2, x, y)) color = muted;
    if (text_pixel(pc ? "CONNECTED" : "DISCONNECTED", 153, 213, 2, x, y))
        color = pc ? on : off;
    if (text_pixel("DUALSENSE", 153, 285, 2, x, y)) color = muted;
    if (text_pixel(controller ? "CONNECTED" : "DISCONNECTED", 153, 310, 2, x, y))
        color = controller ? on : off;
    const int battery_x = (LCD_WIDTH - (int)strlen(battery_text) * 6) / 2;
    if (text_pixel(battery_text, battery_x, 362, 1, x, y))
        color = low_blink ? rgb565(255, 110, 80) : muted;
    if (text_pixel("PAIR: PS + CREATE", 138, 390, 2, x, y)) color = muted;
    return color;
}

static esp_err_t init_panel(void)
{
    uint16_t hw_version = 0;
    esp_err_t err = esp_efuse_read_field_blob(ESP_EFUSE_USER_DATA, &hw_version,
                                               sizeof(hw_version) * 8U);
    if (err != ESP_OK) return err;
    gpio_num_t lcd_sclk;
    gpio_num_t lcd_reset;
    if (hw_version == 0x0100) {
        lcd_sclk = GPIO_NUM_44;
        lcd_reset = GPIO_NUM_42;
    } else if (hw_version == 0x0101 || hw_version == 0x0102) {
        lcd_sclk = GPIO_NUM_42;
        lcd_reset = GPIO_NUM_44;
    } else {
        ESP_LOGE(TAG, "Unsupported board revision in eFuse: 0x%04x", hw_version);
        return ESP_ERR_NOT_SUPPORTED;
    }

    const gpio_config_t power = {
        .pin_bit_mask = 1ULL << 60,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&power), TAG, "configure LCD rail control");
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_60, 0), TAG, "enable LCD rail");
    vTaskDelay(pdMS_TO_TICKS(80));

    const spi_bus_config_t bus = CO5300_PANEL_BUS_QSPI_CONFIG(
        lcd_sclk, LCD_DATA0, LCD_DATA1, LCD_DATA2, LCD_DATA3,
        LCD_WIDTH * LCD_LINES_PER_CHUNK * sizeof(uint16_t));
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO), TAG,
                        "initialize LCD QSPI bus");

    esp_lcd_panel_io_spi_config_t io_config =
        CO5300_PANEL_IO_QSPI_CONFIG(LCD_CS, NULL, NULL);
    io_config.pclk_hz = 40 * 1000 * 1000;
    io_config.trans_queue_depth = 2;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &s_io), TAG,
        "create LCD panel IO");

    static const uint8_t fe_page_20[] = {0x20};
    static const uint8_t mode_10[] = {0x10};
    static const uint8_t mode_a0[] = {0xA0};
    static const uint8_t fe_page_00[] = {0x00};
    static const uint8_t mode_80[] = {0x80};
    static const uint8_t color_55[] = {0x55};
    static const uint8_t brightness_20[] = {0x20};
    static const uint8_t value_ff[] = {0xFF};
    static const uint8_t window_xy[] = {0x00, 0x00, 0x01, 0xDF};
    static const co5300_lcd_init_cmd_t init_cmds[] = {
        {0x11, NULL, 0, 600},
        {0xFE, fe_page_20, 1, 0},
        {0x19, mode_10, 1, 0},
        {0x1C, mode_a0, 1, 0},
        {0xFE, fe_page_00, 1, 0},
        {0xC4, mode_80, 1, 0},
        {0x3A, color_55, 1, 0},
        {0x53, brightness_20, 1, 0},
        {0x51, value_ff, 1, 0},
        {0x63, value_ff, 1, 0},
        {0x2A, window_xy, 4, 0},
        {0x2B, window_xy, 4, 0},
    };
    const co5300_vendor_config_t vendor = {
        .init_cmds = init_cmds,
        .init_cmds_size = sizeof(init_cmds) / sizeof(init_cmds[0]),
        .flags.use_qspi_interface = true,
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = lcd_reset,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_BIG,
        .bits_per_pixel = 16,
        .vendor_config = (void *)&vendor,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_co5300(s_io, &panel_config, &s_panel),
                        TAG, "create CO5300 panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "reset CO5300");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "initialize CO5300");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(s_panel, 0, 0), TAG, "set panel gap");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG,
                        "turn on CO5300");
    ESP_LOGI(TAG, "CO5300 initialized for board revision 0x%04x", hw_version);
    return ESP_OK;
}

esp_err_t ds5_display_start(void)
{
    if (s_started) return ESP_OK;
    s_pixels = heap_caps_malloc(LCD_WIDTH * LCD_LINES_PER_CHUNK * sizeof(uint16_t),
                                MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!s_pixels) return ESP_ERR_NO_MEM;
    esp_err_t err = init_panel();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LCD init failed: %s", esp_err_to_name(err));
        return err;
    }
    s_started = true;
    return ESP_OK;
}

void ds5_display_update(bool pc_connected, bool controller_connected,
                        bool battery_valid, uint8_t battery_percent,
                        bool battery_charging)
{
    const bool low_blink = controller_connected && battery_valid &&
        !battery_charging && battery_percent <= 10 &&
        ((esp_timer_get_time() / 500000) & 1);
    if (!s_started || (s_have_last_state && s_last_pc == pc_connected &&
                       s_last_controller == controller_connected &&
                       s_last_battery_valid == battery_valid &&
                       s_last_battery_percent == battery_percent &&
                       s_last_battery_charging == battery_charging &&
                       s_last_low_blink == low_blink)) return;
    s_last_low_blink = low_blink;
    s_last_pc = pc_connected;
    s_last_controller = controller_connected;
    s_last_battery_valid = battery_valid;
    s_last_battery_percent = battery_percent;
    s_last_battery_charging = battery_charging;
    s_have_last_state = true;
    char battery_text[24];
    if (!controller_connected || !battery_valid) {
        memcpy(battery_text, "BATTERY --", sizeof("BATTERY --"));
    } else if (battery_charging) {
        snprintf(battery_text, sizeof(battery_text), "BATTERY %u%% +",
                 (unsigned)battery_percent);
    } else {
        snprintf(battery_text, sizeof(battery_text), "BATTERY %u%%",
                 (unsigned)battery_percent);
    }

    for (int y0 = 0; y0 < LCD_HEIGHT; y0 += LCD_LINES_PER_CHUNK) {
        for (int row = 0; row < LCD_LINES_PER_CHUNK; ++row) {
            const int y = y0 + row;
            for (int x = 0; x < LCD_WIDTH; ++x)
                s_pixels[row * LCD_WIDTH + x] = pixel_color(
                    x, y, pc_connected, controller_connected, battery_text, low_blink);
        }
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(s_panel, 0, y0, LCD_WIDTH,
                                                  y0 + LCD_LINES_PER_CHUNK,
                                                  s_pixels));
    }
    ESP_LOGI(TAG, "Status screen updated: PC USB %s, DualSense %s, battery %s",
             pc_connected ? "connected" : "disconnected",
             controller_connected ? "connected" : "disconnected",
             battery_valid ? (battery_charging ? "charging" : "available") : "unknown");
}
