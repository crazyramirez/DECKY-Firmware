#pragma once
#include "FreeRTOS.h"
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) {
  static int x;
  return &x;
}
#define xSemaphoreTake(a, b) (1)
#define xSemaphoreGive(a) (1)
