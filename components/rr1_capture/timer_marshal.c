#include "timer_marshal.h"
#include "cpu_idle.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "timer_hist.h"
#include "timer_mqtt.h"
#include <stdbool.h>
#include <stdint.h>
#include <sys/param.h>

#define MQ_MARSHAL_TASK_STACK_SIZE 4096
#define MQ_MARSHAL_TASK_PRIORITY 3

static const char *TAG = "rr1_capture";
static TaskHandle_t mqMarshalTaskHandle = NULL;

static void mqMarshalTask(void *pvParameters);
static void logIdleStats(void);

void timerMarshalInit(void) {
  BaseType_t taskCreated =
      xTaskCreate(&mqMarshalTask, "mq_marshal", MQ_MARSHAL_TASK_STACK_SIZE,
                  NULL, MQ_MARSHAL_TASK_PRIORITY, &mqMarshalTaskHandle);
  if (taskCreated != pdPASS) {
    ESP_LOGE(TAG, "timerMarshalInit: failed to create mq marshal task");
    mqMarshalTaskHandle = NULL;
  }
}

void scheduleMqPubDataList(int delayMs) {
  if (mqMarshalTaskHandle) {
    xTaskNotify(mqMarshalTaskHandle, (uint32_t)delayMs, eSetValueWithOverwrite);
  }
}

static void mqMarshalTask(void *pvParameters) {
  (void)pvParameters;

  const uint64_t noPublishDeadlineMs = UINT64_MAX;
  const uint32_t creditIntervalMs = 15000;
  const uint32_t idleCalcIntervalMs = 10000;
  uint64_t nowMs = esp_timer_get_time() / 1000;
  uint64_t nextCreditMs = nowMs;
  uint64_t nextIdleCalcMs = nowMs;
  uint64_t nextPublishHealthMs =
      timerHistNextHealthDueMs(getXmitHistBacklog(), nowMs);
  uint64_t nextPublishMs = noPublishDeadlineMs;
  bool publishCreditStarved = false;

  while (1) {
    uint64_t nextWakeMs =
        MIN(nextCreditMs, MIN(nextIdleCalcMs, nextPublishHealthMs));
    nextWakeMs = MIN(nextWakeMs, nextPublishMs);
    TickType_t waitTicks =
        pdMS_TO_TICKS(nextWakeMs > nowMs ? (uint32_t)(nextWakeMs - nowMs) : 0);

    uint32_t delayMs = 0;
    BaseType_t notified = xTaskNotifyWait(0, UINT32_MAX, &delayMs, waitTicks);

    nowMs = esp_timer_get_time() / 1000;
    if (notified == pdTRUE) {
      uint64_t requestedPublishMs = nowMs + delayMs;
      if (requestedPublishMs < nextPublishMs) {
        nextPublishMs = requestedPublishMs;
      }
    }

    if (nowMs >= nextIdleCalcMs) {
      logIdleStats();
      nextIdleCalcMs = nowMs + idleCalcIntervalMs;
    }

    bool shouldPublish = false;
    if (nextPublishMs != noPublishDeadlineMs && nowMs >= nextPublishMs) {
      shouldPublish = true;
      nextPublishMs = noPublishDeadlineMs;
    }
    if (nowMs >= nextCreditMs) {
      incMqttPublishCredits();
      nextCreditMs = nowMs + creditIntervalMs;
      if (publishCreditStarved) {
        shouldPublish = true;
      }
    }
    if (nowMs >= nextPublishHealthMs) {
      int backlog = getXmitHistBacklog();
      bool healthDue = isHealthDue(backlog);
      shouldPublish = shouldPublish || healthDue;
      nextPublishHealthMs = healthDue
                                ? nowMs + timerHistGetHealthIntervalMs(backlog)
                                : timerHistNextHealthDueMs(backlog, nowMs);
    }

    if (shouldPublish) {
      publishCreditStarved = getMqttPublishCredits() < 1;
      mqPubDataList();
    }
  }
}

static void logIdleStats(void) {
  statsRecap_t recap = {};
  updateCpuIdleStats(&recap);
  ESP_LOGI(TAG, "idleCalc: cpu  percent %d idle percent %d",
           (int)recap.cpu_used_percent, (int)recap.cpu_idle_percent);
}
