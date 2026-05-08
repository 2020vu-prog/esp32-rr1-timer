/* OTA example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
// #include "protocol_examples_common.h"
#include "esp_crt_bundle.h"
#include "string.h"

#include "nvs.h"
#include "nvs_flash.h"
// #include "protocol_examples_common.h"
#include <sys/socket.h>
#if CONFIG_EXAMPLE_CONNECT_WIFI
#include "esp_wifi.h"
#endif
#include "rr1_blink.h"
#include "rr1_ota.h"
#include "rr1_wifi.h"
#define HASH_LEN 32

static const char *TAG = "simple_ota";
extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_ca_cert_pem_end");

#define OTA_URL_SIZE 256
char latest_ota_etag[64] = {0};
esp_err_t _http_etag_event_handler(esp_http_client_event_t *evt) {
  switch (evt->event_id) {
  case HTTP_EVENT_ON_HEADER:
    // evt->header_key and evt->header_value contain the pair
    printf("Header: %s = %s\n", evt->header_key, evt->header_value);
    if (strcasecmp(evt->header_key, "ETag") == 0) {
      printf("ETag value: %s\n", evt->header_value);
      strncpy(latest_ota_etag, evt->header_value, sizeof(latest_ota_etag) - 1);
      latest_ota_etag[sizeof(latest_ota_etag) - 1] = '\0';
      ESP_LOGI(TAG, "Captured ETag: %s", latest_ota_etag);
    }
    break;
  default:
    break;
  }
  return ESP_OK;
}
bool firmware_update_available(const char *ota_url) {
  esp_http_client_config_t config = {
      .url = ota_url,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .event_handler = _http_etag_event_handler,
      .keep_alive_enable = true,
      .method = HTTP_METHOD_HEAD, // Set method to HEAD

  };

  esp_http_client_handle_t client = esp_http_client_init(&config);

  // 2. Execute the request
  esp_err_t err = esp_http_client_perform(client);

  if (err == ESP_OK) {
    // 3. Retrieve status and headers
    int status_code = esp_http_client_get_status_code(client);
    int64_t content_length = esp_http_client_get_content_length(client);

    ESP_LOGI(TAG, "HTTP HEAD Status = %d, content_length = %lld", status_code,
             content_length);
  } else {
    ESP_LOGE(TAG, "HTTP HEAD request failed: %s", esp_err_to_name(err));
  }

  // 4. Clean up resources
  esp_http_client_cleanup(client);
  char current_etag[64];
  nvs_get_firmware_etag(current_etag, sizeof(current_etag));
  if (strlen(latest_ota_etag) > 0 &&
      strcmp(latest_ota_etag, current_etag) != 0) {
    ESP_LOGI(TAG, "Firmware update is available (ETag changed) [%s]->[%s]",
             current_etag, latest_ota_etag);

    return true;
  }
  return false;
}
esp_err_t _http_ota_event_handler(esp_http_client_event_t *evt) {
  switch (evt->event_id) {
  case HTTP_EVENT_ERROR:
    ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
    break;
  case HTTP_EVENT_ON_CONNECTED:
    ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
    break;
  case HTTP_EVENT_HEADER_SENT:
    ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
    break;
  case HTTP_EVENT_ON_HEADER:
    ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key,
             evt->header_value);
    break;
  case HTTP_EVENT_ON_DATA:
    ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
    break;
  case HTTP_EVENT_ON_FINISH:
    ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
    break;
  case HTTP_EVENT_DISCONNECTED:
    ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
    break;
  case HTTP_EVENT_REDIRECT:
    ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
    break;
  }
  return ESP_OK;
}
int simple_ota_attempt() {
  int rc = -1;
  ESP_LOGI(TAG, "Starting OTA example task");
  char dns_host[64];
  nvs_get_rr1_host(dns_host, sizeof(dns_host));
  char ota_url[OTA_URL_SIZE];
  snprintf(ota_url, OTA_URL_SIZE, "https://%s/firmware/esp32-rr1-timer.bin",
           dns_host);
  ESP_LOGI(TAG, "Constructed OTA URL: %s", ota_url);
  if (firmware_update_available(ota_url)) {
    ESP_LOGI(TAG, "Firmware update is available at %s", ota_url);
  } else {
    ESP_LOGI(TAG, "No firmware update needed at %s", ota_url);
    rc = 0;
    goto ota_done;
  }

  esp_http_client_config_t config = {
      .url = ota_url,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .event_handler = _http_ota_event_handler,
      .keep_alive_enable = true,
  };

#ifdef CONFIG_EXAMPLE_SKIP_COMMON_NAME_CHECK
  config.skip_cert_common_name_check = true;
#endif

  esp_https_ota_config_t ota_config = {
      .http_config = &config,
  };
  ESP_LOGI(TAG, "Attempting to download update from %s", config.url);
  esp_err_t ret = esp_https_ota(&ota_config);
  if (ret == ESP_OK) {
    nvs_set_firmware_etag(latest_ota_etag); // TODO: only update if we actually

    ESP_LOGW(TAG, "OTA Succeed, Rebooting...");
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    esp_restart();
  } else {
    ESP_LOGE(TAG, "Firmware upgrade failed");
    rc = -1;
  }

ota_done:
  ESP_LOGI(TAG, "OTA task finished");

  return rc;
}
void simple_ota_example_task(void *pvParameter) {
  for (int i = 0; i < 5; i++) {
    while (get_error_priority(ERROR_PRI_WIFI_CONNECTION)) {
      ESP_LOGW(TAG,
               "simple_ota_example_task: waiting for Wi-Fi connection before "
               "attempt %d",
               i + 1);

      vTaskDelay(10000 / portTICK_PERIOD_MS); // Wait before retrying
    }
    int wifi_transition_count = get_transition_count(ERROR_PRI_WIFI_CONNECTION);
    ESP_LOGI(TAG, "simple_ota_example_task: attempt %d", i + 1);
    int rc = simple_ota_attempt();
    if (rc == 0) {
      break; // Success, exit the loop
    }

    if (get_transition_count(ERROR_PRI_WIFI_CONNECTION) >
        wifi_transition_count) {
      ESP_LOGW(TAG,
               "simple_ota_example_task: Wi-Fi connection issue detected "
               "during attempt %d",
               i + 1);
      continue;
    }

    ESP_LOGW(TAG, "simple_ota_example_task: attempt %d failed, retrying...",
             i + 1);
  }
  ESP_LOGW(TAG, "simple_ota_example_task: attempt DONE");
  vTaskDelete(NULL);
}

static void print_sha256(const uint8_t *image_hash, const char *label) {
  char hash_print[HASH_LEN * 2 + 1];
  hash_print[HASH_LEN * 2] = 0;
  for (int i = 0; i < HASH_LEN; ++i) {
    sprintf(&hash_print[i * 2], "%02x", image_hash[i]);
  }
  ESP_LOGI(TAG, "%s %s", label, hash_print);
}

void get_sha256_of_partitions(void) {
  uint8_t sha_256[HASH_LEN] = {0};
  esp_partition_t partition;

  // get sha256 digest for bootloader
  partition.address = ESP_BOOTLOADER_OFFSET;
  partition.size = ESP_PARTITION_TABLE_OFFSET;
  partition.type = ESP_PARTITION_TYPE_APP;
  esp_partition_get_sha256(&partition, sha_256);
  print_sha256(sha_256, "SHA-256 for bootloader: ");

  // get sha256 digest for running partition
  esp_partition_get_sha256(esp_ota_get_running_partition(), sha_256);
  print_sha256(sha_256, "SHA-256 for current firmware: ");
}
