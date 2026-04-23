
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
esp_err_t deltaTaskStats(statsSnapshot_t *start, statsSnapshot_t *end,
                         statsRecap_t *recap);
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

  ESP_LOGI(TAG, "Getting task stats with array size %d",
           snapshot->_array_size_allocated);
  // Get current task states
  snapshot->_array_size = uxTaskGetSystemState(
      snapshot->_array, snapshot->_array_size_allocated, &snapshot->_run_time);
  if (snapshot->_array_size == 0) {
    ret = ESP_ERR_INVALID_SIZE;
    ESP_LOGE(TAG, "Failed to get task states");
    return ret;
  }
  ESP_LOGI(TAG, "Got %d task states, runtime: %d", snapshot->_array_size,
           (int)snapshot->_run_time);
  return ret;
}
esp_err_t deltaTaskStats(statsSnapshot_t *start, statsSnapshot_t *end,
                         statsRecap_t *recap) {
  esp_err_t ret = ESP_OK;
  recap->cpu_used_percent = 0;

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
        // start->_array[i].xHandle = NULL;
        // end->_array[j].xHandle = NULL;

        start->_array[i].xTaskNumber =
            0; // mark matched to avoid confusion with deleted/created tasks in
               // unmatched loop below
        end->_array[j].xTaskNumber =
            0; // mark matched to avoid confusion with deleted/created tasks in
               // unmatched loop below
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
      recap->cpu_used_percent += percentage_time;
    }
  }

  // Print unmatched tasks
  for (int i = 0; i < start->_array_size; i++) {
    if (start->_array[i].xTaskNumber != 0) {
      printf("| %s | Deleted\n", start->_array[i].pcTaskName);
    } else {
      start->_array[i].xTaskNumber = 0x01; // reset for re-use in next snapshot
    }
  }
  for (int i = 0; i < end->_array_size; i++) {
    if (end->_array[i].xTaskNumber != 0) {
      printf("| %s | Created\n", end->_array[i].pcTaskName);
    } else {
      end->_array[i].xTaskNumber = 0x01; // reset for re-use in next snapshot
    }
  }

  return ret;
}
void getCpuIdleStats(statsRecap_t *recap) {
  esp_err_t ret = ESP_OK;
  recap->cpu_used_percent = -1;
  if (!start_snapshot || !end_snapshot) {
    ESP_LOGE(TAG, "Snapshots not initialized");
    cpuIdleInit();
    return; // let caller try again after initialization (time needs to pass
            // between snapshots to get meaningful data)
  }
  ret = espgetTaskStats(end_snapshot);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get task stats: %s", esp_err_to_name(ret));
    return;
  }
  ret = deltaTaskStats(start_snapshot, end_snapshot, recap);
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