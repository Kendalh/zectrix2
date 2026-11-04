// Host shim: just enough FreeRTOS for the shell (threads + queues, 1 tick = 1 ms).
#pragma once
#include <cstdint>
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef void* QueueHandle_t;
typedef void* TaskHandle_t;
#define portMAX_DELAY 0xFFFFFFFFu
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
