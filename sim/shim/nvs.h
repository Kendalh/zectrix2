// Host shim for the NVS calls zos::Settings uses; the store lives in memory.
#pragma once
#include <cstddef>
#include <cstdint>
#include "esp_err.h"
#define ESP_ERR_NVS_NOT_FOUND 0x1102
typedef uint32_t nvs_handle_t;
typedef enum { NVS_READONLY, NVS_READWRITE } nvs_open_mode_t;
esp_err_t nvs_open(const char* ns, nvs_open_mode_t mode, nvs_handle_t* out);
void nvs_close(nvs_handle_t handle);
esp_err_t nvs_commit(nvs_handle_t handle);
esp_err_t nvs_get_str(nvs_handle_t h, const char* key, char* out, size_t* length);
esp_err_t nvs_set_str(nvs_handle_t h, const char* key, const char* value);
esp_err_t nvs_get_i32(nvs_handle_t h, const char* key, int32_t* out);
esp_err_t nvs_set_i32(nvs_handle_t h, const char* key, int32_t value);
esp_err_t nvs_get_u8(nvs_handle_t h, const char* key, uint8_t* out);
esp_err_t nvs_set_u8(nvs_handle_t h, const char* key, uint8_t value);
