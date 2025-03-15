#include "esp_log.h"

#include <stdio.h>
#include <unistd.h>
#include <sys/lock.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"
#include "ui/ui.h"

static const char *TAG = "MyDisplay";

/* LCD size */
#define DISP_HOR_RES 320 // 320
#define DISP_VER_RES 172 // 240

/* LCD settings */
#define DISP_DRAW_BUFF_HEIGHT 50

/* LCD pins */
#define DISP_SPI_NUM SPI3_HOST
#define DISP_GPIO_SCLK GPIO_NUM_12
#define DISP_GPIO_MOSI GPIO_NUM_11
#define DISP_GPIO_RST -1 // Not connected
#define DISP_GPIO_DC GPIO_NUM_47
#define DISP_GPIO_CS GPIO_NUM_45
#define DISP_GPIO_BL GPIO_NUM_48

#define BUFFER_SIZE (DISP_HOR_RES * DISP_VER_RES * sizeof(uint16_t) / 10)

static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_panel_io_handle_t io_handle = NULL;
static lv_display_t *display = NULL;

static void *buf1 = NULL;
static void *buf2 = NULL;

// this gets called when the DMA transfer of the buffer data has completed
static bool notify_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_display_t *disp = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp); // I have tried to change this to the global display variable, no change
    return false;
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    int x1 = area->x1;
    int x2 = area->x2;
    int y1 = area->y1 + 34; // Offset image to compensate for smaller 172px resolution
    int y2 = area->y2 + 34; // Offset image to compensate for smaller 172px resolution

    // uncomment the following line if the colors are wrong
    lv_draw_sw_rgb565_swap(px_map, (x2 + 1 - x1) * (y2 + 1 - y1)); // I have tried with and without this

    esp_lcd_panel_draw_bitmap((esp_lcd_panel_handle_t)lv_display_get_user_data(disp), x1, y1, x2 + 1, y2 + 1, px_map);
}

static esp_err_t lvgl_init(void)
{
    lv_init();

    display = lv_display_create(DISP_HOR_RES, DISP_VER_RES);

    buf1 = heap_caps_calloc(1, BUFFER_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    buf2 = heap_caps_calloc(1, BUFFER_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);

    lv_display_set_buffers(display, buf1, buf2, BUFFER_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_display_set_user_data(display, panel_handle);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);

    lv_display_set_flush_cb(display, flush_cb);

    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = notify_flush_ready,
    };
    /* Register done callback */
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_register_event_callbacks(io_handle, &cbs, display), TAG, "esp_lcd_panel_io_register_event_callbacks error"); // I have tried to use
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel_handle), TAG, "esp_lcd_panel_init error");

    // Hardware rotate 90°
    uint8_t madctl = 0x60; // Rotate reg 0x00 0x60 0xC0 0xA0
    esp_lcd_panel_io_tx_param(io_handle, 0x36, &madctl, 1);

    esp_lcd_panel_io_tx_param(io_handle, 0x21, NULL, 0); // Inverted color fix (to get normal colors)

    return ESP_OK;
}

static esp_err_t display_init(void)
{
    // LCD backlight and DC
    gpio_config_t gpio_cfg = {
        .pin_bit_mask = (1ULL << DISP_GPIO_BL) | (1ULL << DISP_GPIO_DC),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&gpio_cfg));

    gpio_set_level(DISP_GPIO_BL, 1); // Turn on backlight
    gpio_set_level(DISP_GPIO_DC, 1); // Default to data mode

    // LCD initialization
    ESP_LOGD(TAG, "Initialize SPI bus");
    spi_bus_config_t buscfg = {};
    buscfg.sclk_io_num = DISP_GPIO_SCLK;
    buscfg.mosi_io_num = DISP_GPIO_MOSI;
    buscfg.miso_io_num = GPIO_NUM_NC;
    buscfg.quadwp_io_num = GPIO_NUM_NC;
    buscfg.quadhd_io_num = GPIO_NUM_NC;
    buscfg.max_transfer_sz = BUFFER_SIZE; // DISP_HOR_RES * DISP_DRAW_BUFF_HEIGHT * sizeof(uint16_t);
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO), TAG, "SPI init failed");

    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = DISP_GPIO_DC,
        .cs_gpio_num = DISP_GPIO_CS,
        .pclk_hz = 80 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &io_handle), TAG, "SPI init failed");

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = DISP_GPIO_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .flags = {.reset_active_high = 0}};

    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle), TAG, "Display init failed");

    // Reset the display
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));

    // Initialize LCD panel
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    // Vendor specific settings

    // Turn on the screen
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    return ESP_OK;
}

static void lvgl_tick_increment(void *arg)
{
    // Tell LVGL how many milliseconds have elapsed
    lv_tick_inc(2);
}

static esp_err_t lvgl_tick_init(void)
{
    esp_timer_handle_t tick_timer;

    // Tick interface for LVGL (using esp_timer to generate 2ms periodic event)
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &lvgl_tick_increment,
        .name = "LVGL tick",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&lvgl_tick_timer_args, &tick_timer), TAG, "Creating LVGL timer filed!");
    return esp_timer_start_periodic(tick_timer, 2 * 1000); // 2 ms
}

static void lvgl_task(void *arg)
{

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    esp_log_level_set("lcd_panel", ESP_LOG_VERBOSE);
    esp_log_level_set("lcd_panel.st7789", ESP_LOG_VERBOSE);
    esp_log_level_set(TAG, ESP_LOG_VERBOSE);

    esp_err_t ret = display_init();

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "ST7789 failed to initilize");
        while (1)
            ;
    }
    ret = lvgl_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "LVGL Display failed to initialize");
        while (1)
            ;
    }

    ret = lvgl_tick_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Timer failed to initialize");
        while (1)
            ;
    }
    ui_init();

    // Handle LVGL tasks
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
        lv_task_handler();
        ui_tick();
    }
}

void app_main()
{

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    TaskHandle_t taskHandle = NULL;
    xTaskCreatePinnedToCore(lvgl_task, "LVGL task", 8192, NULL, 4, &taskHandle, 0); // stack, params, prio, handle, core

    while (true)
    {

        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}