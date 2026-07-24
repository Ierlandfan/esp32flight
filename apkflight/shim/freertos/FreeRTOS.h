#pragma once

/* FreeRTOS shim over pthreads: just enough for the esp32flight core. */

#include <stdbool.h>
#include <stdint.h>

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;

#define pdTRUE  1
#define pdFALSE 0
#define pdPASS  1

#define portMAX_DELAY 0xffffffffU
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define portTICK_PERIOD_MS 1
