#pragma once
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <inttypes.h>

typedef struct {
  int cpu_used_percent;
  int cpu_idle_percent;
} statsRecap_t;
int getRecentCpuIdlePercentAverage();
void updateCpuIdleStats(statsRecap_t *recap);