#include "esp_ws28xx.h"

static const uint16_t timing_bits[16] = {
    0x1111, 0x7111, 0x1711, 0x7711, 0x1171, 0x7171, 0x1771, 0x7771,
    0x1117, 0x7117, 0x1717, 0x7717, 0x1177, 0x7177, 0x1777, 0x7777};

esp_err_t ws28xx_init(ws28xx_t** ctx, int pin, led_strip_model_t model, int num_of_leds,
                      CRGB *led_buffer, uint16_t *dma_buffer, spi_settings_t* spi_settings) {
    esp_err_t err = ESP_OK;
    if (*ctx == NULL) {
        *ctx = malloc(sizeof(ws28xx_t));
        if (ctx == NULL) {
            return ESP_ERR_NO_MEM;
        }
        (*ctx)->ownership_bits = (1 << WS28XX_CTX_OWNED_BIT);
    } else {
        memset(*ctx, 0, sizeof(ws28xx_t));
    }
    ws28xx_t* _ctx = *ctx;
    _ctx->n_of_leds = num_of_leds;
    _ctx->led_model = model;
    // Insrease if something breaks. values are less than recommended in
    // datasheets but seem stable
    _ctx->reset_delay = (model == WS2812B) ? 3 : 30;
    // 12 bytes for each led + bytes for initial zero and reset state
    _ctx->dma_buf_size = num_of_leds * 12 + (_ctx->reset_delay + 1) * 2;
    if (led_buffer == NULL) {
        led_buffer = malloc(sizeof(CRGB) * num_of_leds);
        if (led_buffer == NULL) {
            ws28xx_destroy(_ctx);
            *ctx = NULL;
            return ESP_ERR_NO_MEM;
        }
        _ctx->ownership_bits |= WS28XX_PIXEL_BUFFER_OWNED_BIT;
    }
    _ctx->ws28xx_pixels = led_buffer;

    if (spi_settings == NULL) {
        spi_settings = malloc(sizeof(spi_settings_t));
        if (spi_settings == NULL) {
            ws28xx_destroy(_ctx);
            *ctx = NULL;
            return ESP_ERR_NO_MEM;
        }
        *spi_settings = (spi_settings_t){
            .host = SPI2_HOST,
            .dma_chan = SPI_DMA_CH_AUTO,
            .buscfg =
                {
                    .miso_io_num = -1,
                    .mosi_io_num = pin,
                    .sclk_io_num = -1,
                    .quadwp_io_num = -1,
                    .quadhd_io_num = -1,
                    .max_transfer_sz = _ctx->dma_buf_size
                },
            .devcfg =
                {
                    .clock_speed_hz = 3.2 * 1000 * 1000, // Clock out at 3.2 MHz
                    .mode = 0,                           // SPI mode 0
                    .spics_io_num = -1,                  // CS pin
                    .queue_size = 1,
                    .command_bits = 0,
                    .address_bits = 0,
                    .flags = SPI_DEVICE_TXBIT_LSBFIRST,
                },
        };

        err = spi_bus_initialize(spi_settings->host, &spi_settings->buscfg,
                                spi_settings->dma_chan);
        if (err != ESP_OK) {
            free(spi_settings);
            ws28xx_destroy(_ctx);
            *ctx = NULL;
            return err;
        }
        err = spi_bus_add_device(spi_settings->host, &spi_settings->devcfg,
                                &spi_settings->spi);
        if (err != ESP_OK) {
            free(spi_settings);
            ws28xx_destroy(_ctx);
            *ctx = NULL;
            return err;
        }
        _ctx->ownership_bits |= WS28XX_SPI_SETTINGS_OWNED_BIT;
    }
    _ctx->spi_settings = spi_settings;

    // Critical to be DMA memory.
    if (dma_buffer == NULL) {
        dma_buffer = heap_caps_malloc(_ctx->dma_buf_size, MALLOC_CAP_DMA);
        if (dma_buffer == NULL) {
            ws28xx_destroy(_ctx);
            *ctx = NULL;
            return ESP_ERR_NO_MEM;
        }
        _ctx->ownership_bits |= WS28XX_DMA_BUFFER_OWNED_BIT;
    }
    _ctx->dma_buffer = dma_buffer;

    return ESP_OK;
}

void ws28xx_fill_all(ws28xx_t* ctx, CRGB color) {
    for (int i = 0; i < ctx->n_of_leds; i++) {
        ctx->ws28xx_pixels[i] = color;
    }
}

esp_err_t ws28xx_update(ws28xx_t* ctx) {
    esp_err_t err;
    int n = 0;
    memset(ctx->dma_buffer, 0, ctx->dma_buf_size);
    ctx->dma_buffer[n++] = 0;
    for (int i = 0; i < ctx->n_of_leds; i++) {
        // Data you want to write to each LEDs
        uint32_t temp = ctx->ws28xx_pixels[i].num;
        if (ctx->led_model == WS2815) {
            // Red
            ctx->dma_buffer[n++] = timing_bits[0x0f & (temp >> 4)];
            ctx->dma_buffer[n++] = timing_bits[0x0f & (temp)];

            // Green
            ctx->dma_buffer[n++] = timing_bits[0x0f & (temp >> 12)];
            ctx->dma_buffer[n++] = timing_bits[0x0f & (temp) >> 8];
        } else {
            // Green
            ctx->dma_buffer[n++] = timing_bits[0x0f & (temp >> 12)];
            ctx->dma_buffer[n++] = timing_bits[0x0f & (temp) >> 8];

            // Red
            ctx->dma_buffer[n++] = timing_bits[0x0f & (temp >> 4)];
            ctx->dma_buffer[n++] = timing_bits[0x0f & (temp)];
        }
        // Blue
        ctx->dma_buffer[n++] = timing_bits[0x0f & (temp >> 20)];
        ctx->dma_buffer[n++] = timing_bits[0x0f & (temp) >> 16];
    }
    for (int i = 0; i < ctx->reset_delay; i++) {
        ctx->dma_buffer[n++] = 0;
    }

    err = spi_device_transmit(ctx->spi_settings->spi, &(spi_transaction_t){
                                                    .length = ctx->dma_buf_size * 8,
                                                    .tx_buffer = ctx->dma_buffer,
                                                });
    return err;
}

void ws28xx_destroy(ws28xx_t* ctx) {
    if (ctx->ownership_bits & WS28XX_DMA_BUFFER_OWNED_BIT) {
        free(ctx->dma_buffer);
    }

    if (ctx->ownership_bits & WS28XX_PIXEL_BUFFER_OWNED_BIT) {
        free(ctx->ws28xx_pixels);
    }

    if (ctx->ownership_bits & WS28XX_SPI_SETTINGS_OWNED_BIT) {
        free(ctx->spi_settings);
    }

    if (ctx->ownership_bits & WS28XX_CTX_OWNED_BIT) {
        free(ctx);
    }
}
