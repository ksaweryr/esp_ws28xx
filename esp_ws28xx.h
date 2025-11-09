#ifndef MAIN_ESP_WS28XX_H_
#define MAIN_ESP_WS28XX_H_
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    union {
        struct {
            union {
                uint8_t r;
                uint8_t red;
            };

            union {
                uint8_t g;
                uint8_t green;
            };

            union {
                uint8_t b;
                uint8_t blue;
            };
        };

        uint8_t raw[3];
        uint32_t num;
    };
} CRGB;

typedef struct {
    spi_host_device_t host;
    spi_device_handle_t spi;
    int dma_chan;
    spi_device_interface_config_t devcfg;
    spi_bus_config_t buscfg;
} spi_settings_t;

typedef enum {
    WS2812B = 0,
    WS2815,
} led_strip_model_t;

#define WS28XX_CTX_OWNED_BIT 1
#define WS28XX_DMA_BUFFER_OWNED_BIT 2
#define WS28XX_PIXEL_BUFFER_OWNED_BIT 4
#define WS28XX_SPI_SETTINGS_OWNED_BIT 8

typedef struct {
    uint8_t ownership_bits;
    uint16_t *dma_buffer;
    CRGB *ws28xx_pixels;
    int n_of_leds, reset_delay, dma_buf_size;
    led_strip_model_t led_model;
    spi_settings_t* spi_settings;
} ws28xx_t;

esp_err_t ws28xx_init(ws28xx_t** ctx, int pin, led_strip_model_t model, int num_of_leds,
                      CRGB *led_buffer, uint16_t *dma_buffer, spi_settings_t* spi_settings);
void ws28xx_fill_all(ws28xx_t* ctx, CRGB color);
esp_err_t ws28xx_update(ws28xx_t* ctx);
void ws28xx_destroy(ws28xx_t* ctx);

#endif /* MAIN_ESP_WS28XX_H_ */
