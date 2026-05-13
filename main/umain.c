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

#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
// #include "protocol_examples_common.h"
#include <sys/socket.h>
#if CONFIG_EXAMPLE_CONNECT_WIFI
#include "esp_wifi.h"
#endif
#include "rr1_ota.h"
#include "rr1_wifi.h"

#include "esp_app_desc.h"
#include "sdkconfig.h"

#include "timer_capture.h"

#include "get_mqtt_creds.h"
#include "nmea_example.h"
#include "quad_uint32.h"
#include "timer_i2c.h"
#include "timer_mqtt.h"
#include "timer_stdin.h"

// Use the macro: CONFIG_APP_PROJECT_VER

static const char *TAG = "umain";

void app_main(void) {
  vTaskDelay(pdMS_TO_TICKS(100));

  ESP_LOGI(TAG, "OTA example app_main start");
  // Initialize NVS.
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // 1.OTA app partition table has a smaller NVS partition size than the
    // non-OTA partition table. This size mismatch may cause NVS initialization
    // to fail. 2.NVS partition contains data in new format and cannot be
    // recognized by this version of code. If this happens, we erase NVS
    // partition and initialize NVS again.
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);

  get_sha256_of_partitions();

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  xTaskCreate(&capture_main_xtask, "capture_main", 8192, NULL, 5, NULL);

  /* This helper function configures Wi-Fi or Ethernet, as selected in
   * menuconfig. Read "Establishing Wi-Fi or Ethernet Connection" section in
   * examples/protocols/README.md for more information about this function.
   */
  // init_timer_stdin(); stdin messes up flash from usb
  rr1WifiProv();
  nvs_dumprr1();

  // ESP_ERROR_CHECK(example_connect());

#if CONFIG_EXAMPLE_CONNECT_WIFI
  /* Ensure to disable any WiFi power save mode, this allows best throughput
   * and hence timings for overall OTA operation.
   */
  esp_wifi_set_ps(WIFI_PS_NONE);
#endif // CONFIG_EXAMPLE_CONNECT_WIFI

  esp_wifi_set_ps(WIFI_PS_NONE);
  https_request_creds();
  mqtt_app_start();
  rr1_i2c_init();

  testem();
  // capture_main();

  ESP_LOGI(TAG, "app_main: Hello World! runumber: %s", GITHUB_RUN_NUMBER);
  // TODO:  check for new version. no repeat updates to same ver!
  xTaskCreate(&simple_ota_example_task, "ota_task", 8192, NULL, 5, NULL);
  xTaskCreate(&nmea_main, "nmea_main", 8192, NULL, 5, NULL);

  const esp_app_desc_t *ad = esp_app_get_description();
  while (1) {
    // ESP_LOGI(TAG, "umain Hello World! %s", CONFIG_APP_PROJECT_VER);
    ESP_LOGI(TAG, "umain version TEST859! %s", ad->version);
    vTaskDelay(5000 / portTICK_PERIOD_MS);
  }
}