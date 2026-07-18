#include "mqtt_cli.h"

#include "esp_log.h"
#include "timer_i2c_tap.h"
#include <cJSON.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "mqtt_cli";

static bool eventTopicEquals(esp_mqtt_event_handle_t event, const char *topic) {
  if (!event->topic || !topic) {
    return false;
  }

  size_t topic_len = strlen(topic);
  return event->topic_len == topic_len &&
         strncmp(event->topic, topic, topic_len) == 0;
}

static void handleCliCommandObject(const cJSON *command, int index) {
  const cJSON *cmd = cJSON_GetObjectItemCaseSensitive(command, "cmd");
  if (cJSON_IsString(cmd) && cmd->valuestring &&
      strcmp(cmd->valuestring, "tap_threshold") == 0) {
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(command, "value");
    if (!cJSON_IsNumber(value) || value->valueint < 0 ||
        value->valueint > 0x7F) {
      ESP_LOGW(TAG,
               "CLI command[%d] tap_threshold ignored: value must be 0..127",
               index);
      return;
    }

    ESP_LOGI(TAG, "CLI command[%d]: tap_threshold=%d", index, value->valueint);
    ESP_LOGI(TAG, "CLI command[%d]: invoking rr1_i2c_tap_reinit", index);
    rr1_i2c_tap_reinit((uint8_t)value->valueint);
    return;
  }

  char *json = cJSON_PrintUnformatted(command);
  if (!json) {
    ESP_LOGW(TAG, "CLI command[%d]: failed to render object", index);
    return;
  }

  ESP_LOGI(TAG, "CLI command[%d]: %s", index, json);
  cJSON_free(json);
}

static void parseMqttCliJsonPayload(const char *payload) {
  cJSON *root = cJSON_Parse(payload);
  if (!root) {
    const char *error_ptr = cJSON_GetErrorPtr();
    ESP_LOGW(TAG, "CLI JSON parse failed near: %s",
             error_ptr ? error_ptr : "(unknown)");
    return;
  }

  if (cJSON_IsObject(root)) {
    handleCliCommandObject(root, 0);
    cJSON_Delete(root);
    return;
  }

  if (!cJSON_IsArray(root)) {
    ESP_LOGW(TAG, "CLI JSON payload must be an object or array of objects");
    cJSON_Delete(root);
    return;
  }

  int index = 0;
  cJSON *command = NULL;
  cJSON_ArrayForEach(command, root) {
    if (cJSON_IsObject(command)) {
      handleCliCommandObject(command, index);
    } else {
      ESP_LOGW(TAG, "CLI command[%d] ignored: expected object", index);
    }
    index++;
  }

  cJSON_Delete(root);
}

void mqttCliHandleData(esp_mqtt_event_handle_t event, const char *cli_topic) {
  if (!eventTopicEquals(event, cli_topic)) {
    return;
  }

  ESP_LOGI(TAG, "CLI MQTT data topic=%.*s len=%d total=%d offset=%d",
           event->topic_len, event->topic, event->data_len,
           event->total_data_len, event->current_data_offset);

  if (event->current_data_offset != 0 ||
      event->data_len != event->total_data_len) {
    ESP_LOGW(TAG, "CLI MQTT fragmented payload ignored");
    return;
  }

  char *payload = calloc((size_t)event->data_len + 1, sizeof(char));
  if (!payload) {
    ESP_LOGE(TAG, "Failed to allocate %d byte CLI MQTT payload",
             event->data_len + 1);
    return;
  }

  memcpy(payload, event->data, event->data_len);
  ESP_LOGI(TAG, "CLI MQTT payload: %s", payload);
  parseMqttCliJsonPayload(payload);
  free(payload);
}
