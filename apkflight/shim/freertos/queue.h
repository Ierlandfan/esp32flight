#pragma once
/* Minimal FreeRTOS queue shim for the desktop/Android build: a mutex +
 * condvar ring buffer, enough for the tile worker's job queue. */
#include "freertos/FreeRTOS.h"

typedef struct shim_queue *QueueHandle_t;

QueueHandle_t xQueueCreate(unsigned len, unsigned item_size);
BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t ticks);
BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t ticks);
void vQueueDelete(QueueHandle_t q);
