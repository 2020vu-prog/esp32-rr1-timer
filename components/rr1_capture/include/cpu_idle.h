#pragma once
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <inttypes.h>

typedef struct {
  int cpu_used_percent;
} statsRecap_t;

void getCpuIdleStats(statsRecap_t *recap);