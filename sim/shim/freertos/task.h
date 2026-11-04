#pragma once
#include "freertos/FreeRTOS.h"
typedef void (*TaskFunction_t)(void*);
BaseType_t xTaskCreate(TaskFunction_t fn, const char* name, uint32_t stack,
                       void* arg, uint32_t priority, TaskHandle_t* handle);
void vTaskDelay(TickType_t ticks);
inline UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t) { return 8192; }
