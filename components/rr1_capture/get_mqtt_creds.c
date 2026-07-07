#include "esp_crt_bundle.h"
#include "esp_http_client.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "get_mqtt_creds.h"
#include "rr1_wifi.h"
#include "timer_blink.h"
#include "timer_mqtt.h"
#include <cJSON.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#define BUFLEN 10000
const static char *TAG = "get_mqtt_creds";
typedef struct _rr1_creds {
  char *authUrl;
  char *mqtt_host;
  char *mqtt_cert;
  char *mqtt_key;
} _rr1_creds;
_rr1_creds creds = {
    .authUrl = NULL,
    .mqtt_host = NULL,
    .mqtt_cert = NULL,
    .mqtt_key = NULL,
};
void https_request_auth(char *host_name);
void https_request_discover(char *host_name);
void free_creds() {
  if (creds.authUrl) {
    free(creds.authUrl);
    creds.authUrl = NULL;
  }
  if (creds.mqtt_host) {
    free(creds.mqtt_host);
    creds.mqtt_host = NULL;
  }

  if (creds.mqtt_cert) {
    free(creds.mqtt_cert);
    creds.mqtt_cert = NULL;
  }
  if (creds.mqtt_key) {
    free(creds.mqtt_key);
    creds.mqtt_key = NULL;
  }
}
void malloc_and_strcpy(char **dest, const char *src) {
  if (src == NULL) {
    free(*dest);
    *dest = NULL;
    return;
  }
  size_t len = strlen(src) + 1;
  *dest = malloc(len);
  if (*dest != NULL) {
    strncpy(*dest, src, len);
  } else {
    ESP_LOGE(TAG, "Failed to allocate memory for string copy");
  }
}
void copyJsonString(const cJSON *jsonObj, char **dest, const char *key) {
  const cJSON *jsonMember = NULL;

  jsonMember = cJSON_GetObjectItemCaseSensitive(jsonObj, key);

  if (cJSON_IsString(jsonMember) && (jsonMember->valuestring != NULL)) {
    ESP_LOGI(TAG, "Checking [%s] jsonStr \"%s\"\n", key,
             jsonMember->valuestring);
    malloc_and_strcpy(dest, jsonMember->valuestring);
  } else {
    ESP_LOGW(TAG, "Key [%s] not found or not a string in JSON\n", key);
  }
}
int parse_authApiKey(const char *const jsonStr) {
  int status = 0;
  cJSON *jsonObj = cJSON_Parse(jsonStr);
  if (jsonObj == NULL) {
    const char *error_ptr = cJSON_GetErrorPtr();
    if (error_ptr != NULL) {
      ESP_LOGI(TAG, "Error before: %s\n", error_ptr);
    }
    status = 0;
    goto end;
  }

  copyJsonString(jsonObj, &creds.mqtt_host, "mqttHost");
  copyJsonString(jsonObj, &creds.mqtt_cert, "certificatePem");
  copyJsonString(jsonObj, &creds.mqtt_key, "privateKeyPem");

end:
  cJSON_Delete(jsonObj);
  return status;
}

int parse_discover(const char *const jsonStr) {
  int status = 0;
  cJSON *jsonObj = cJSON_Parse(jsonStr);
  if (jsonObj == NULL) {
    const char *error_ptr = cJSON_GetErrorPtr();
    if (error_ptr != NULL) {
      ESP_LOGI(TAG, "Error before: %s\n", error_ptr);
    }
    status = 0;
    goto end;
  }
  copyJsonString(jsonObj, &creds.authUrl, "authUrl");

end:
  cJSON_Delete(jsonObj);
  return status;
}

esp_err_t accum_event_handler(esp_http_client_event_t *evt) {
  static size_t buf_used = 0;
  if (evt == NULL) {
    buf_used = 0;
    return ESP_OK;
  }

  if (evt->user_data == NULL) {
    ESP_LOGE(TAG, "Invalid event data");
    return ESP_FAIL;
  }

  switch (evt->event_id) {
  case HTTP_EVENT_ON_DATA:
    if (evt->data == NULL || evt->data_len <= 0) {
      break;
    }

    size_t remaining = BUFLEN - buf_used;
    size_t copy_len = MIN((size_t)evt->data_len, remaining);
    if (copy_len > 0) {
      memcpy((char *)evt->user_data + buf_used, evt->data, copy_len);
      buf_used += copy_len;
      ((char *)evt->user_data)[buf_used] = '\0';
    }

    ESP_LOGI(TAG, "HTTP data received: %d bytes%s", evt->data_len,
             esp_http_client_is_chunked_response(evt->client) ? " chunked"
                                                              : "");
    if (copy_len < (size_t)evt->data_len) {
      ESP_LOGE(TAG, "Credential response truncated at %d bytes", BUFLEN);
    }

    break;
  // Handle other events like HTTP_EVENT_ERROR or HTTP_EVENT_ON_FINISH
  default:
    break;
  }
  return ESP_OK;
}
void https_request_creds(void) {
  char host_name[12];
  get_device_hostname(host_name, 12);

  set_error_priority(ERROR_PRI_CREDENTIALS, true);

  int64_t start_us = esp_timer_get_time();
  https_request_discover(host_name);
  int64_t discover_done_us = esp_timer_get_time();
  https_request_auth(host_name);
  int64_t auth_done_us = esp_timer_get_time();
  ESP_LOGI(TAG,
           "https_request_creds latency discover=%" PRId64 " ms auth=%" PRId64
           " ms total=%" PRId64 " ms",
           (discover_done_us - start_us) / 1000,
           (auth_done_us - discover_done_us) / 1000,
           (auth_done_us - start_us) / 1000);
  if (creds.mqtt_host && creds.mqtt_cert && creds.mqtt_key) {
    set_error_priority(ERROR_PRI_CREDENTIALS, false);
  }
}
void https_request_auth(char *host_name) {

  char *buffer = malloc(BUFLEN + 1);
  if (buffer == NULL) {
    ESP_LOGE(TAG, "Failed to allocate credential auth response buffer");
    return;
  }
  memset(buffer, 0, BUFLEN + 1);

  char authUrl[512];
  snprintf(authUrl, sizeof(authUrl), "%s", creds.authUrl);
  char *authPath = strstr(authUrl, "/auth");
  if (authPath) {
    //*authPath = '\0'; // Terminate the string at the start of "/auth"
    strcpy(authPath, "/authApiKey");
  }
  // 1. Create JSON payload
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "TIMER", host_name);
  char apikey[256] = "";
  nvs_get_rr1_apikey(apikey, sizeof(apikey));
  ESP_LOGI(TAG, "NVS returned apiKey [%s]", apikey);
  cJSON_AddStringToObject(root, "apiKey", apikey);

  char *post_data = cJSON_PrintUnformatted(root);

  // 2. HTTP Client Configuration
  accum_event_handler(NULL); // Reset static buffer index

  esp_http_client_config_t config = {
      .method = HTTP_METHOD_POST,
      .url = authUrl,
      .crt_bundle_attach =
          esp_crt_bundle_attach, // Uses built-in certificate bundle
      .transport_type = HTTP_TRANSPORT_OVER_SSL,
      .user_data = buffer, // Buffer to store response
      .event_handler = accum_event_handler,

  };
  esp_http_client_handle_t client = esp_http_client_init(&config);

  // 3. Set Header and Post Data
  esp_http_client_set_header(client, "Content-Type", "application/json");
  esp_http_client_set_post_field(client, post_data, strlen(post_data));

  // 4. Perform Request
  int64_t perform_start_us = esp_timer_get_time();
  esp_err_t err = esp_http_client_perform(client);
  int64_t perform_ms = (esp_timer_get_time() - perform_start_us) / 1000;
  if (err == ESP_OK) {
    ESP_LOGI(TAG,
             "HTTP POST Status = %d, content_length = %lld, latency=%" PRId64
             " ms",
             esp_http_client_get_status_code(client),
             esp_http_client_get_content_length(client), perform_ms);
    ESP_LOGI(TAG, "POST Received datalen: %d", strlen(buffer));
    ESP_LOGI(TAG, "POST Received data: %s", buffer);

    parse_authApiKey(buffer);
  } else {
    ESP_LOGE(TAG, "HTTP POST request failed after %" PRId64 " ms: %s",
             perform_ms, esp_err_to_name(err));
  }

  // 5. Cleanup
  esp_http_client_cleanup(client);
  cJSON_Delete(root);
  free(post_data);
  free(buffer);
}

void https_request_discover(char *host_name) {
  ESP_LOGI(TAG, "Requesting MQTT credentials...237");

  char *buffer = malloc(BUFLEN + 1);
  if (buffer == NULL) {
    ESP_LOGE(TAG, "Failed to allocate credential discover response buffer");
    return;
  }
  memset(buffer, 0, BUFLEN + 1);
  char dns_host[64];
  nvs_get_rr1_host(dns_host, sizeof(dns_host));
  char url[256];
  snprintf(url, sizeof(url), "https://%s/app/iot/discover", dns_host);
  ESP_LOGI(TAG, "Discover URL: %s", url);
  accum_event_handler(NULL); // Reset static buffer index

  esp_http_client_config_t config = {
      .url = url,
      .crt_bundle_attach =
          esp_crt_bundle_attach, // Uses built-in certificate bundle
      .transport_type = HTTP_TRANSPORT_OVER_SSL,
      .user_data = buffer, // Buffer to store response
      .event_handler = accum_event_handler,

  };

  esp_http_client_handle_t client = esp_http_client_init(&config);
  esp_http_client_set_header(client, "x-rr1-timer", host_name);
  esp_http_client_set_method(client, HTTP_METHOD_GET);
  int64_t perform_start_us = esp_timer_get_time();
  esp_err_t err = esp_http_client_perform(client);
  int64_t perform_ms = (esp_timer_get_time() - perform_start_us) / 1000;

  if (err == ESP_OK) {
    int clen = esp_http_client_get_content_length(client);

    ESP_LOGI(TAG,
             "HTTPS Status = %d, content_length = %d, latency=%" PRId64 " ms\n",
             esp_http_client_get_status_code(client), clen, perform_ms);
    if (buffer) {
      ESP_LOGI(TAG, "Received data: %s", buffer);
      parse_discover(buffer);
    } else {
      ESP_LOGE(TAG, "Failed to read response");
    }
  }

  else {
    ESP_LOGE(TAG, "Error perform HTTPS request after %" PRId64 " ms: %s\n",
             perform_ms, esp_err_to_name(err));
  }
  esp_http_client_cleanup(client);

  if (buffer)
    free(buffer);
}

const char *get_mqtt_host() { return creds.mqtt_host; }
const char *get_mqtt_cert() { return creds.mqtt_cert; }
const char *get_mqtt_key() { return creds.mqtt_key; }
