#include "ui.h"
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <dirent.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "sdkconfig.h"

// SD-MMC interface using esp-idf:
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"

// RGB and button drivers from https://github.com/UncleRus/esp-idf-lib.git
#include "esp_idf_lib_helpers.h"
#include "button.h"
#include <unistd.h>

// Display driver from https://github.com/lovyan03/LovyanGFX.git
#include "LovyanGFX.hpp"
#include "../components/lvgl/lvgl.h"
#include "thaisaraban.c"
// #include "thaisarabanBig.c"

#define TAG "dongle"

#define LV_TICK_PERIOD_MS 1
// SD-MMC interface
#define SDMMC_MOUNTPOINT "/sdcard"
#define SDMMC_D0 (gpio_num_t)14
#define SDMMC_D1 (gpio_num_t)17
#define SDMMC_D2 (gpio_num_t)21
#define SDMMC_D3 (gpio_num_t)18
#define SDMMC_CLK (gpio_num_t)12
#define SDMMC_CMD (gpio_num_t)16
/*********************
 *      DEFINES
 *********************/
#define HEADER_HEIGHT 30
#define FOOTER_HEIGHT 30
LV_FONT_DECLARE(thaisaraban);
LV_FONT_DECLARE(thaisarabanBig);
// extern lv_font_t thaisaraban;
led_strip_spi_t stripSPI_;
lv_obj_t *status;
lv_obj_t *message;
lv_obj_t *devices;
/******************
 *  LV DEFINES
 ******************/
static const lv_font_t *font_large;
static const lv_font_t *font_normal;
static const lv_font_t *font_symbol;

static lv_obj_t *panel_header;
static lv_obj_t *panel_status;
static lv_obj_t *panel_container;

static lv_obj_t *label_title;
static lv_obj_t *label_message;

static lv_obj_t *icon_storage;
static lv_obj_t *icon_wifi;
static lv_obj_t *icon_battery;

/******************
 *  LVL STYLES
 ******************/
static lv_style_t style_message;
static lv_style_t style_title;
static lv_style_t style_storage;
static lv_style_t style_wifi;
static lv_style_t style_battery;

/******************
 *  LVL ANIMATION
 ******************/
static lv_anim_t anim_labelscroll;
/*Change to your screen resolution*/
static const uint16_t screenWidth = 160;
static const uint16_t screenHeight = 80;

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[2][screenWidth * 10];
static lv_color_t buf2[screenWidth * 10];

static lv_disp_t *disp;
static lv_theme_t *theme_current;
static lv_color_t bg_theme_color;
static SemaphoreHandle_t xGuiSemaphore = NULL;
static TaskHandle_t g_lvgl_task_handle;

/* ----------------------*/
class LGFX_LiLyGo_TDongleS3 : public lgfx::LGFX_Device
{
    lgfx::Panel_ST7735S _panel_instance;
    lgfx::Bus_SPI _bus_instance;
    lgfx::Light_PWM _light_instance;

public:
    LGFX_LiLyGo_TDongleS3(void)
    {
        {
            auto cfg = _bus_instance.config();

            cfg.spi_host = SPI3_HOST;          // SPI2_HOST is in use by the RGB led
            cfg.spi_mode = 0;                  // Set SPI communication mode (0 ~ 3)
            cfg.freq_write = 27000000;         // SPI clock when sending (max 80MHz, rounded to 80MHz divided by an integer)
            cfg.freq_read = 16000000;          // SPI clock when receiving
            cfg.spi_3wire = true;              // Set true when receiving on the MOSI pin
            cfg.use_lock = false;              // Set true when using transaction lock
            cfg.dma_channel = SPI_DMA_CH_AUTO; // Set the DMA channel to use (0=not use DMA / 1=1ch / 2=ch / SPI_DMA_CH_AUTO=auto setting)

            cfg.pin_sclk = DISPLAY_SCLK; // set SPI SCLK pin number
            cfg.pin_mosi = DISPLAY_MOSI; // Set MOSI pin number for SPI
            cfg.pin_miso = DISPLAY_MISO; // Set MISO pin for SPI (-1 = disable)
            cfg.pin_dc = DISPLAY_DC;     // Set SPI D/C pin number (-1 = disable)

            _bus_instance.config(cfg);              // Apply the setting value to the bus.
            _panel_instance.setBus(&_bus_instance); // Sets the bus to the panel.
        }

        {
            auto cfg = _panel_instance.config(); // Obtain the structure for display panel settings.

            cfg.pin_cs = DISPLAY_CS;     // Pin number to which CS is connected (-1 = disable)
            cfg.pin_rst = DISPLAY_RST;   // pin number where RST is connected (-1 = disable)
            cfg.pin_busy = DISPLAY_BUSY; // pin number to which BUSY is connected (-1 = disable)

            cfg.panel_width = DISPLAY_HEIGHT; // actual displayable width. Note: width/height swapped due to the rotation
            cfg.panel_height = DISPLAY_WIDTH; // Actual displayable height Note: width/height swapped due to the rotation
            cfg.offset_x = 26;                // Panel offset in X direction
            cfg.offset_y = 1;                 // Y direction offset amount of the panel
            cfg.offset_rotation = 1;          // Rotation direction value offset 0~7 (4~7 are upside down)
            cfg.dummy_read_pixel = 8;         // Number of bits for dummy read before pixel read
            cfg.dummy_read_bits = 1;          // Number of dummy read bits before non-pixel data read
            cfg.readable = true;              // set to true if data can be read
            cfg.invert = true;
            cfg.rgb_order = false;
            cfg.dlen_16bit = false; // Set to true for panels that transmit data length in 16-bit units with 16-bit parallel or SPI
            cfg.bus_shared = true;  // If the bus is shared with the SD card, set to true (bus control with drawJpgFile etc.)

            // Please set the following only when the display is shifted with a driver with a variable number of pixels such as ST7735 or ILI9163.
            cfg.memory_width = 132;  // Maximum width supported by driver IC
            cfg.memory_height = 160; // Maximum height supported by driver IC

            _panel_instance.config(cfg);
        }

        {
            auto cfg = _light_instance.config();

            cfg.pin_bl = DISPLAY_LEDA; // pin number to which the backlight is connected
            cfg.invert = true;         // true to invert backlight brightness
            cfg.freq = 12000;          // Backlight PWM frequency
            cfg.pwm_channel = 7;       // PWM channel number to use

            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance);
        }

        setPanel(&_panel_instance);
    }
};

static LGFX_LiLyGo_TDongleS3 lcd;

//
// helper functions to generate colors
//
uint32_t led_effect_color_wheel(uint8_t pos)
{
    pos = 255 - pos;
    if (pos < 85)
    {
        return ((uint32_t)(255 - pos * 3) << 16) | ((uint32_t)(0) << 8) | (pos * 3);
    }
    else if (pos < 170)
    {
        pos -= 85;
        return ((uint32_t)(0) << 16) | ((uint32_t)(pos * 3) << 8) | (255 - pos * 3);
    }
    else
    {
        pos -= 170;
        return ((uint32_t)(pos * 3) << 16) | ((uint32_t)(255 - pos * 3) << 8) | (0);
    }
}

rgb_t led_effect_color_wheel_rgb(uint8_t pos)
{
    uint32_t next_color;
    rgb_t next_pixel;

    next_color = led_effect_color_wheel(pos);
    next_pixel.r = (next_color >> 16) & 0xff;
    next_pixel.g = (next_color >> 8) & 0xff;
    next_pixel.b = (next_color);
    return next_pixel;
}

static esp_err_t rainbow(led_strip_spi_t *strip)
{
    static uint8_t pos = 0;
    esp_err_t err = ESP_FAIL;
    rgb_t color;

    color = led_effect_color_wheel_rgb(pos);

    if ((err = led_strip_spi_fill(strip, 0, strip->length, color)) != ESP_OK)
    {
        ESP_LOGE(TAG, "led_strip_spi_fill(): %s", esp_err_to_name(err));
        goto fail;
    }
    pos += 1;
fail:
    return err;
}

//
// task for the rgb led
//
void RGBLED(void *pvParam)
{
    led_strip_spi_t strip = LED_STRIP_SPI_DEFAULT();
    static spi_device_handle_t device_handle;

    strip.mosi_io_num = RGBLED_DI;
    strip.sclk_io_num = RGBLED_CI;
    strip.length = LEDSTRIP_LEN;
    strip.device_handle = device_handle;
    strip.max_transfer_sz = LED_STRIP_SPI_BUFFER_SIZE(LEDSTRIP_LEN);
    strip.clock_speed_hz = 1000000 * 10; // 10Mhz

    ESP_LOGI(TAG, "Initializing LED strip");
    ESP_ERROR_CHECK(led_strip_spi_init(&strip));
    ESP_ERROR_CHECK(led_strip_spi_flush(&strip));

    while (42)
    {
        ESP_ERROR_CHECK(rainbow(&strip));
        ESP_ERROR_CHECK(led_strip_spi_flush(&strip));
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void gui_task(void *args);

/*** Function declaration ***/
void display_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p);

#ifdef TOUCH_ENABLED
void touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data);
#endif

static void lv_tick_task(void *arg);

esp_err_t lv_display_init()
{
    esp_err_t ret;
    // Setting display to landscape
    // if (lcd.width() < lcd.height()) lcd.setRotation(lcd.getRotation() ^ 2);

    lcd.setBrightness(16);
    lcd.setColorDepth(24);

    lcd.fillScreen(TFT_BLACK);

    /* LVGL : Setting up buffer to use for display */
    lv_disp_draw_buf_init(&draw_buf, buf, buf2, screenWidth * 10);

    /*** LVGL : Setup & Initialize the display device driver ***/
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = screenWidth;
    disp_drv.ver_res = screenHeight;
    disp_drv.flush_cb = display_flush;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.sw_rotate = 1;
    disp = lv_disp_drv_register(&disp_drv);

    /* Create and start a periodic timer interrupt to call lv_tick_inc */
    const esp_timer_create_args_t lv_periodic_timer_args = {
        .callback = &lv_tick_task,
        .name = "periodic_gui"};
    esp_timer_handle_t lv_periodic_timer;
    ESP_ERROR_CHECK(esp_timer_create(&lv_periodic_timer_args, &lv_periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lv_periodic_timer, LV_TICK_PERIOD_MS * 1000));

    // Setup theme
    theme_current = lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE),
                                          lv_palette_main(LV_PALETTE_RED),
                                          LV_USE_THEME_DEFAULT, /*Light or dark mode*/
                                          &lv_font_montserrat_14);

    // lv_disp_set_theme(disp, th); /*Assign the theme to the display*/
    bg_theme_color = theme_current->flags & LV_USE_THEME_DEFAULT ? lv_palette_darken(LV_PALETTE_GREY, 4) : lv_palette_lighten(LV_PALETTE_GREY, 1);

    xGuiSemaphore = xSemaphoreCreateMutex();
    if (!xGuiSemaphore)
    {
        ESP_LOGE(TAG, "Create mutex for LVGL failed");
        if (lv_periodic_timer)
            esp_timer_delete(lv_periodic_timer);
        return ESP_FAIL;
    }

    int err = xTaskCreatePinnedToCore(gui_task, "lv gui", 1024 * 8, NULL, 5, &g_lvgl_task_handle, 1);

    if (!err)
    {
        ESP_LOGE(TAG, "Create task for LVGL failed");
        if (lv_periodic_timer)
            esp_timer_delete(lv_periodic_timer);
        return ESP_FAIL;
    }

    esp_timer_start_periodic(lv_periodic_timer, LV_TICK_PERIOD_MS * 1000U);
    return ESP_OK;
}

// Display callback to flush the buffer to screen
void display_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    lcd.startWrite();
    lcd.setAddrWindow(area->x1, area->y1, w, h);
    lcd.pushPixels((uint16_t *)&color_p->full, w * h, true);
    lcd.endWrite();

    lv_disp_flush_ready(disp);
}

/* Setting up tick task for lvgl */
static void lv_tick_task(void *arg)
{
    (void)arg;
    lv_tick_inc(LV_TICK_PERIOD_MS);
}

static void gui_task(void *args)
{
    ESP_LOGI(TAG, "Start to run LVGL");
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(10));

        /* Try to take the semaphore, call lvgl related function on success */
        if (pdTRUE == xSemaphoreTake(xGuiSemaphore, portMAX_DELAY))
        {
            lv_task_handler();
            // lv_timer_handler_run_in_period(5); /* run lv_timer_handler() every 5ms */
            xSemaphoreGive(xGuiSemaphore);
        }
    }
}

void lvgl_acquire(void)
{
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    if (g_lvgl_task_handle != task)
    {
        xSemaphoreTake(xGuiSemaphore, portMAX_DELAY);
    }
}

void lvgl_release(void)
{
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    if (g_lvgl_task_handle != task)
    {
        xSemaphoreGive(xGuiSemaphore);
    }
}

void ShowStatus(char *statusText, uint32_t color_bar, uint32_t color_text)
{
    char status_text[256]; // = "Status : Wifi Connected";
    snprintf(status_text, sizeof(status_text), "สถานะ : %s", statusText);
    lv_style_set_bg_color(&style_title, lv_color_hex(color_bar));
    // lv_obj_set_style_bg_color(status, lv_color_hex(0x00ff00), LV_PART_MAIN);
    // lv_style_set_bg_color(&style_title, lv_color_hex(0x00ff00));
    lv_obj_set_style_text_color(status, lv_color_hex(color_text), LV_PART_MAIN);
    lv_label_set_text(status, status_text);
    LEDstripSPIOn(&stripSPI_, color_bar);
}
void ShowWifiSSID(char *SSID, char *Password)
{
    lcd.setColor(0xffffffu);
    lcd.drawRect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lcd.setTextColor(0xffffff, 0x000000);
    lcd.setTextSize(1);
    lcd.setCursor(2, 30); //+13
    char showText[256];
    snprintf(showText, sizeof(showText), "SSID : %s", SSID);
    lcd.println(showText);
    // lcd.setCursor(2, 30);
    // char showText2[256];
    // snprintf(showText2, sizeof(showText2), "Password : %s", Password);
    // lcd.println(showText2);
}
void ShowTempText(char *temp)
{
    LEDstripSPIOn(&stripSPI_, 0x0000ff);
    lv_label_set_text(message, temp);
}
void ShowTempNone()
{
    LEDstripSPIOn(&stripSPI_, 0x0000ff);
    lv_label_set_text(message, "");
}
void ShowNumDevice(int devicesc)
{
    char showText[256];
    snprintf(showText, sizeof(showText), "%d", devicesc);

    lv_label_set_text(devices, showText);
}
void Sleep()
{
    lcd.setBrightness(4);
    LEDstripSPIOn(&stripSPI_, 0x000000);
}
void Wakeup()
{
    lcd.setBrightness(64);
}

void ShowTemp(double temp)
{
    if (temp >= 35.4 && temp <= 37.4)
    {
        lv_obj_set_style_text_color(message, lv_color_hex(0x00ff00), LV_PART_MAIN);
        // lcd.setTextColor(0x00ff00u, 0x000000);
    }
    else if (temp >= 37.5 && temp <= 38.4)
    {
        lv_obj_set_style_text_color(message, lv_color_hex(0xBDF516), LV_PART_MAIN);
        // lcd.setTextColor(0xBDF516u, 0x000000);
    }
    else if (temp >= 38.5 && temp <= 39.4)
    {
        lv_obj_set_style_text_color(message, lv_color_hex(0xFFFF00), LV_PART_MAIN);
        // lcd.setTextColor(0xFFFF00u, 0x000000);
    }
    else if (temp >= 39.5)
    {
        lv_obj_set_style_text_color(message, lv_color_hex(0xff0000), LV_PART_MAIN);
        // lcd.setTextColor(0xff0000u, 0x000000);
    }
    else
    {

        lv_obj_set_style_text_color(message, lv_color_hex(0x0000FF), LV_PART_MAIN);
        // lcd.setTextColor(0x0000FF, 0x000000);
    }
    char status_text[256]; // = "Status : Wifi Connected";
    snprintf(status_text, sizeof(status_text), "%.1f °C", temp);

    lv_label_set_text_fmt(message, status_text);
    // lv_refr_now(NULL);
    LEDstripSPIOn(&stripSPI_, 0x00ff00);
}
void LEDstripSPI_setup(void)
{
    esp_err_t err = led_strip_spi_install();
    if (err != ESP_OK)
        ESP_LOGI(TAG, "led_strip_spi_install(): %s", esp_err_to_name(err));
    if (err == ESP_OK)
        ESP_LOGI(TAG, "led_strip_spi_install() successful : %s", esp_err_to_name(err));

    led_strip_spi_t strip = LED_STRIP_SPI_DEFAULT();
    static spi_device_handle_t device_handle;

    strip.mosi_io_num = RGBLED_DI;
    strip.sclk_io_num = RGBLED_CI;
    strip.length = LEDSTRIP_LEN;
    strip.device_handle = device_handle;
    strip.max_transfer_sz = LED_STRIP_SPI_BUFFER_SIZE(LEDSTRIP_LEN);
    strip.clock_speed_hz = 1000000 * 0.001; // 0.001Mhz 10Mhz
    strip.host_device = SPI2_HOST;
    stripSPI_ = strip;
}

led_strip_spi_t LEDstripSPIOn(led_strip_spi_t *strip, uint32_t colorLED)
{
    rgb_t COLOR = rgb_from_code(colorLED);

    esp_err_t err;
    // err = led_strip_spi_set_pixel(strip, strip->length - 1, COLOR);
    err = led_strip_spi_set_pixel_brightness(strip, strip->length - 1, COLOR, 12);
    if (err != ESP_OK)
        ESP_LOGE(TAG, "led_strip_spi_fill(): %s", esp_err_to_name(err));
    err = led_strip_spi_flush(strip);
    if (err != ESP_OK)
        ESP_LOGE(TAG, "led_strip_spi_flush(): %s", esp_err_to_name(err));
    return *strip;
}
led_strip_spi_t LEDstripSPI_Init(led_strip_spi_t *strip)
{
    rgb_t color;
    color.r = 0;
    color.g = 0;
    color.b = 0;

    esp_err_t err;
    err = led_strip_spi_init(strip);
    if (err != ESP_OK)
        ESP_LOGE(TAG, "led_strip_spi_init(): %s", esp_err_to_name(err));
    err = led_strip_spi_set_pixel(strip, strip->length - 1, color);
    if (err != ESP_OK)
        ESP_LOGE(TAG, "led_strip_spi_fill(): %s", esp_err_to_name(err));
    err = led_strip_spi_flush(strip);
    if (err != ESP_OK)
        ESP_LOGE(TAG, "led_strip_spi_flush(): %s", esp_err_to_name(err));

    return *strip;
}
void ui_init(void)
{
    // esp_err_t err;
    // sdmmc_card_t *card;
    // const char mount_point[] = SDMMC_MOUNTPOINT;
    // DIR *hDir;
    // struct dirent *eDir;

    ESP_LOGE(TAG, "lcd.init");
    lcd.init();

    ESP_LOGE(TAG, "lv_init");
    lv_init();
    if (lv_display_init() != ESP_OK) // Configure LVGL
    {
        ESP_LOGE(TAG, "LVGL setup failed!!!");
    }

    font_symbol = &lv_font_montserrat_10;
    font_normal = &lv_font_montserrat_14;
    font_large = &lv_font_montserrat_24;

    // DASHBOARD TITLE
    lv_style_init(&style_title);
    lv_style_set_text_font(&style_title, font_large);
    lv_style_set_align(&style_title, LV_ALIGN_OUT_LEFT_TOP);
    lv_style_set_bg_opa(&style_title, LV_OPA_COVER);
    lv_style_set_bg_color(&style_title, lv_color_hex(0x00FF00));
    // lv_style_set_pad_left(&style_title, 15);

    status = lv_label_create(lv_scr_act());

    lv_label_set_long_mode(status, LV_LABEL_LONG_SCROLL_CIRCULAR); /*Circular scroll*/
    lv_obj_set_width(status, 160);
    lv_obj_set_style_text_font(status, &thaisaraban, 0);
    // lv_obj_align(status, LV_ALIGN_OUT_LEFT_TOP, 0, 0);
    lv_obj_add_style(status, &style_title, 0);

    lv_style_init(&style_message);
    lv_style_set_text_font(&style_message, &thaisarabanBig);
    lv_style_set_align(&style_message, LV_ALIGN_CENTER);
    // lv_style_set_pad_top(&style_title, 3);

    message = lv_label_create(lv_scr_act());
    // lv_obj_set_width(message, 160);
    // lv_label_set_long_mode(message, LV_LABEL_LONG_DOT);
    // lv_obj_set_style_text_font(message, &thaisarabanBig, 0);
    lv_obj_set_align(message, LV_ALIGN_CENTER);
    lv_obj_align(message, LV_ALIGN_TOP_MID, 0, 15);
    lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_style(message, &style_message, 0);
    // lv_obj_set_width(message, 160);
    // lv_obj_set_height(message, 80);

    // lv_obj_align(message, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(message, "-°C");

    LEDstripSPI_setup();
    LEDstripSPI_Init(&stripSPI_);
    LEDstripSPIOn(&stripSPI_, 0x000000);
    // lv_style_init(&style_storage);
    // lv_style_set_text_font(&style_storage, font_symbol);
    // lv_style_set_align(&style_storage, LV_ALIGN_OUT_BOTTOM_MID);
    // devices = lv_label_create(lv_scr_act());
    // lv_obj_set_align(devices, LV_ALIGN_OUT_BOTTOM_MID);
    // lv_obj_align(devices, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    // lv_obj_set_style_text_align(devices, LV_ALIGN_OUT_BOTTOM_MID, 0);
    // lv_obj_set_style_text_color(devices, lv_color_hex(0xff0000), LV_PART_MAIN);
    // lv_obj_add_style(devices, &style_title, 0);
    // lv_label_set_text(status, "DASHBOARD");
    // lv_obj_add_style(label_title, &style_title, 0);
    //  lv_label_set_text(status, "test");
    //  lv_obj_set_width(status, 160);
    //  lv_obj_set_height(status, 20);
    //  lv_obj_set_size(status, 160, 20);
    //  lv_obj_align(status, LV_ALIGN_TOP_MID, 0, 0);
    //
    //  Display, ST7735s on SPI bus
    //
    //  if (!lcd.init())
    //  {
    //      ESP_LOGW(TAG, "lcd.init() failed");
    //  }
    //  else
    //  {

    //     lcd.setBrightness(128);

    //     lcd.clear(0x000000);
    //     ShowStatus("Wifi Connected");
    //     ShowWifiSSID("Hadwan", "0878553384");
    //     ShowTemp(30.2);
    //     sleep(2);
    //     ShowTemp(36.4);
    //     sleep(2);
    //     ShowTemp(37.5);
    //     sleep(2);
    //     ShowTemp(38.5);
    //     sleep(2);
    //     ShowTemp(39.5);
    //     ShowWifiSSID("Hadwan", "08785");
    //     sleep(2);
    //     lcd.fillRect(0, 0, 160, 16, 0xffffffu);

    //     // /*Change the active screen's background color*/
    //  //   lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x003a57), LV_PART_MAIN);

    //     // /*Create a white label, set its text and align it to the center*/
    //     // lv_obj_t *label = lv_label_create(lv_scr_act());
    //     // lv_label_set_text(label, "Hello world");
    //     // lv_obj_set_style_text_color(lv_scr_act(), lv_color_hex(0xffffff), LV_PART_MAIN);
    //     // lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    // }
}