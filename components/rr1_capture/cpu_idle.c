
#include "cpu_idle.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

// Increase this if print_real_time_stats returns ESP_ERR_INVALID_SIZE
#define ARRAY_SIZE_OFFSET 5

const char *TAG = "cpu_idle";
typedef struct {
  TaskStatus_t *_array;
  UBaseType_t _array_size;
  UBaseType_t _array_size_allocated;
  configRUN_TIME_COUNTER_TYPE _run_time;
} statsSnapshot_t;

statsSnapshot_t *start_snapshot;
statsSnapshot_t *end_snapshot;
esp_err_t espgetTaskStats(statsSnapshot_t *snapshot);
esp_err_t deltaTaskStats(statsSnapshot_t *start, statsSnapshot_t *end);

statsSnapshot_t *taskStatsInit();

void cpuIdleInit() {
  start_snapshot = taskStatsInit();
  if (start_snapshot == NULL) {
    ESP_LOGE(TAG, "Failed to initialize start snapshot");
    return;
  }
  end_snapshot = taskStatsInit();
  if (end_snapshot == NULL) {
    ESP_LOGE(TAG, "Failed to initialize end snapshot");
    return;
  }
  espgetTaskStats(start_snapshot);
}

statsSnapshot_t *taskStatsInit() {

  statsSnapshot_t *snapshot = malloc(sizeof(statsSnapshot_t));
  // Allocate array to store current task states
  snapshot->_array_size = uxTaskGetNumberOfTasks() + ARRAY_SIZE_OFFSET;
  snapshot->_array = malloc(sizeof(TaskStatus_t) * snapshot->_array_size);
  if (snapshot->_array == NULL) {
    ESP_LOGE(TAG, "Failed to allocate memory for task states");
    return NULL;
  }
  snapshot->_array_size_allocated = snapshot->_array_size;
  return snapshot;
}
esp_err_t espgetTaskStats(statsSnapshot_t *snapshot) {
  esp_err_t ret = ESP_OK;

  // Get current task states
  snapshot->_array_size = uxTaskGetSystemState(
      snapshot->_array, snapshot->_array_size, &snapshot->_run_time);
  if (snapshot->_array_size == 0) {
    ret = ESP_ERR_INVALID_SIZE;
    ESP_LOGE(TAG, "Failed to get task states");
    return ret;
  }
  return ret;
}
esp_err_t deltaTaskStats(statsSnapshot_t *start, statsSnapshot_t *end) {
  esp_err_t ret = ESP_OK;

  // Calculate total_elapsed_time in units of run time stats clock period.
  uint32_t total_elapsed_time = (end->_run_time - start->_run_time);
  if (total_elapsed_time == 0) {
    ret = ESP_ERR_INVALID_STATE;
    ESP_LOGE(TAG, "No time elapsed between snapshots");
    return ret;
  }

  printf("| Task | Run Time | Percentage\n");
  // Match each task in start_array to those in the end_array
  for (int i = 0; i < start->_array_size; i++) {
    int k = -1;
    for (int j = 0; j < end->_array_size; j++) {
      if (start->_array[i].xHandle == end->_array[j].xHandle) {
        k = j;
        // Mark that task have been matched by overwriting their handles
        start->_array[i].xHandle = NULL;
        end->_array[j].xHandle = NULL;
        break;
      }
    }
    // Check if matching task found
    if (k >= 0) {
      uint32_t task_elapsed_time =
          end->_array[k].ulRunTimeCounter - start->_array[i].ulRunTimeCounter;
      uint32_t percentage_time =
          (task_elapsed_time * 100UL) /
          (total_elapsed_time * CONFIG_FREERTOS_NUMBER_OF_CORES);
      printf("| %s | %" PRIu32 " | %" PRIu32 "%%\n",
             start->_array[i].pcTaskName, task_elapsed_time, percentage_time);
    }
  }

  // Print unmatched tasks
  for (int i = 0; i < start->_array_size; i++) {
    if (start->_array[i].xHandle != NULL) {
      printf("| %s | Deleted\n", start->_array[i].pcTaskName);
    }
  }
  for (int i = 0; i < end->_array_size; i++) {
    if (end->_array[i].xHandle != NULL) {
      printf("| %s | Created\n", end->_array[i].pcTaskName);
    }
  }

  return ret;
}
void getCpuIdleStats() {
  esp_err_t ret = ESP_OK;
  ret = espgetTaskStats(end_snapshot);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get task stats: %s", esp_err_to_name(ret));
    return;
  }
  ret = deltaTaskStats(start_snapshot, end_snapshot);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to calculate delta task stats: %s",
             esp_err_to_name(ret));
    return;
  }
  // Update start snapshot for next measurement
  statsSnapshot_t *temp = end_snapshot;
  end_snapshot = start_snapshot;
  start_snapshot = temp;
}