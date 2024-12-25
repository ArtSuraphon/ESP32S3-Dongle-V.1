#include <stdint.h>
#include <led_strip_spi_esp32.h>
#include <led_strip_spi.h>
//
// GPIO configuration
//

// Button hardware configuration:
#define BUTTON_GPIO 0

// RGB led hardware configuration:
#define RGBLED_CI 39
#define RGBLED_DI 40
#define LEDSTRIP_LEN 1

// Display (ST7735s) hardware configuration:
#define DISPLAY_RST 1
#define DISPLAY_DC 2
#define DISPLAY_MOSI 3
#define DISPLAY_CS 4
#define DISPLAY_SCLK 5
#define DISPLAY_LEDA 38
#define DISPLAY_MISO -1
#define DISPLAY_BUSY -1
#define DISPLAY_WIDTH 160
#define DISPLAY_HEIGHT 80

void ui_init(void);
void ShowStatus(char *statusText, uint32_t color_bar, uint32_t color_text);
void ShowTemp(double temp);
void ShowTempNone();
void ShowNumDevice(int devices);
void Sleep();
void Wakeup();
void ShowTempText(char *temp);
led_strip_spi_t LEDstripSPIOn(led_strip_spi_t *strip, uint32_t colorLED);