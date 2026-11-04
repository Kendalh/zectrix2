// Host shim of the SSD2683 driver API. sim_platform.cc implements it with an
// in-memory gray panel that enforces the same refresh rules as the driver.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define ZECTRIX_EPD_PANEL_WIDTH 400
#define ZECTRIX_EPD_PANEL_HEIGHT 300
#define ZECTRIX_EPD_1BPP_FRAME_BYTES 15000
#define ZECTRIX_EPD_4BPP_FRAME_BYTES 60000

typedef struct zectrix_epd_t* zectrix_epd_handle_t;
typedef struct { int x, y, width, height; } zectrix_epd_rect_t;
typedef struct { int unused; } zectrix_epd_config_t;

void zectrix_epd_get_default_config(zectrix_epd_config_t* config);
esp_err_t zectrix_epd_new(const zectrix_epd_config_t* config, zectrix_epd_handle_t* out);
esp_err_t zectrix_epd_power_on(zectrix_epd_handle_t h);
esp_err_t zectrix_epd_power_off(zectrix_epd_handle_t h);
esp_err_t zectrix_epd_refresh_full_1bpp(zectrix_epd_handle_t h, const uint8_t* fb, size_t size);
esp_err_t zectrix_epd_refresh_partial_1bpp(zectrix_epd_handle_t h, const zectrix_epd_rect_t* rect,
                                           const uint8_t* pixels, size_t size);
esp_err_t zectrix_epd_refresh_full_4bpp(zectrix_epd_handle_t h, const uint8_t* fb, size_t size);
