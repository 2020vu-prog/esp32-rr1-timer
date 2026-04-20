
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "string.h"
#include <inttypes.h>
#include <stdio.h>

#include "esp_random.h"
#include "rr1_wifi.h"
const static char *TAG = "nvs_utils";
void nvs_dumprr1() {

  nvs_iterator_t it = NULL;
  //	esp_err_t err;

  /* Initialize NVS partition */
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    /* NVS partition was truncated
     * and needs to be erased */
    ESP_ERROR_CHECK(nvs_flash_erase());

    /* Retry nvs_flash_init */
    ESP_ERROR_CHECK(nvs_flash_init());
  }

  // Start iteration: find the first entry in the default partition, all types,
  // all namespaces
  ESP_ERROR_CHECK(nvs_entry_find("nvs", NULL, NVS_TYPE_ANY, &it));

  esp_err_t res = ESP_OK;
  while (res == ESP_OK) {
    nvs_entry_info_t info;
    nvs_entry_info(it, &info);

    // Process the namespace name (info.namespace_name)
    // You might add this to a list of unique namespace names
    ESP_LOGI(TAG, "Found entry in namespace: %s, key: %s, type: %d\n",
             info.namespace_name, info.key, info.type);

    // Move to the next entry
    res = nvs_entry_next(&it);
  }

  // Release the iterator
  nvs_release_iterator(it);
}

void nvs_get(char *namespace, char *key, char *out_value, size_t max_len) {
  memset(out_value, 0, max_len);

  nvs_handle_t handle;
  esp_err_t err = nvs_open(namespace, NVS_READONLY, &handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error opening NVS handle: %s\n", esp_err_to_name(err));
    return;
  }

  size_t required_size = 0;
  err = nvs_get_str(handle, key, NULL, &required_size);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGI(TAG, "Key '%s' not found in namespace '%s'\n", key, namespace);
    nvs_close(handle);
    return;
  } else if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error getting string size: %s\n", esp_err_to_name(err));
    nvs_close(handle);
    return;
  }

  if (required_size > max_len) {
    ESP_LOGE(
        TAG,
        "Buffer too small for key '%s' in namespace '%s'. Required size: %d\n",
        key, namespace, required_size);
    nvs_close(handle);
    return;
  }

  err = nvs_get_str(handle, key, out_value, &required_size);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error getting string value: %s\n", esp_err_to_name(err));
    nvs_close(handle);
    return;
  }

  ESP_LOGI(TAG, "Value for key '%s' in namespace '%s': %s\n", key, namespace,
           out_value);

  nvs_close(handle);
}
void nvs_set(char *namespace, char *key, char *value) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(namespace, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error opening NVS handle: %s\n", esp_err_to_name(err));
    return;
  }

  err = nvs_set_str(handle, key, value);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error setting string value: %s\n", esp_err_to_name(err));
    nvs_close(handle);
    return;
  }

  err = nvs_commit(handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error committing changes: %s\n", esp_err_to_name(err));
    nvs_close(handle);
    return;
  }

  ESP_LOGI(TAG, "Successfully set key '%s' in namespace '%s' to value '%s'\n",
           key, namespace, value);

  nvs_close(handle);
}
void nvs_get_rr1_host(char *out_value, size_t max_len) {
  nvs_get("rr1", "dns_host", out_value, max_len);
  if (strlen(out_value) == 0) {
    ESP_LOGW(TAG, "rr1 dns_host not found in NVS");
    strncpy(out_value, "test.rr1.us", max_len);
  }
}
void nvs_get_rr1_apikey(char *out_value, size_t max_len) {
  char dns_host[64];
  nvs_get_rr1_host(dns_host, sizeof(dns_host));
  ESP_LOGI(TAG, "Using rr1 dns_host %s to determine api_key\n", dns_host);
  nvs_get(dns_host, "api_key", out_value, max_len);
  if (strlen(out_value) == 0) {
    ESP_LOGW(TAG, "rr1 [%s] api_key not found in NVS", dns_host);
    uint8_t apiRandomBytes[128];
    size_t outlen;
    esp_fill_random(apiRandomBytes, sizeof(apiRandomBytes));
    memset(out_value, 0, max_len);
    mbedtls_base64_encode((unsigned char *)out_value, max_len, &outlen,
                          apiRandomBytes, sizeof(apiRandomBytes));

    nvs_set_rr1_apikey(out_value);
    ESP_LOGI(TAG, "Generated random api_key [%s]", out_value);
    // strncpy(out_value, "", max_len);
  }
}
void nvs_set_rr1_apikey(char *api_key) {
  char dns_host[64];
  nvs_get_rr1_host(dns_host, sizeof(dns_host));
  ESP_LOGI(TAG, "Using rr1 dns_host %s to determine where to set api_key\n",
           dns_host);
  nvs_set(dns_host, "api_key", api_key);
}