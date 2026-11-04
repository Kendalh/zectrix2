// Host shim for ESP-IDF esp_log.h: everything goes to stderr.
#pragma once
#include <cstdio>
#include "esp_err.h"
#define ESP_LOGE(tag, fmt, ...) std::fprintf(stderr, "E (%s) " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) std::fprintf(stderr, "W (%s) " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) std::fprintf(stderr, "I (%s) " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) ((void)0)
