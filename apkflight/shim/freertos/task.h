#pragma once

#include "freertos/FreeRTOS.h"

typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);

/* implemented in platform/shim_rtos.c over pthreads */
BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, int stack,
                       void *arg, int prio, TaskHandle_t *out);
#define xTaskCreatePinnedToCore(fn, name, stack, arg, prio, out, core) \
    xTaskCreate(fn, name, stack, arg, prio, out)

void vTaskDelay(TickType_t ticks);
void vTaskDelete(TaskHandle_t task);   /* NULL = calling task */
