#pragma once
#include "freertos/FreeRTOS.h"
QueueHandle_t xQueueCreate(uint32_t length, uint32_t item_size);
BaseType_t xQueueSend(QueueHandle_t q, const void* item, TickType_t wait);
BaseType_t xQueueSendToFront(QueueHandle_t q, const void* item, TickType_t wait);
BaseType_t xQueueReceive(QueueHandle_t q, void* item, TickType_t wait);
