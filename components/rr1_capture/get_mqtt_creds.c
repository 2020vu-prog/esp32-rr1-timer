#include "esp_http_client.h"
#include "esp_crt_bundle.h"

#include "get_mqtt_creds.h"
#include "esp_log.h"
const static char *TAG = "get_mqtt_creds";
void https_request_creds(void)
{
    ESP_LOGI(TAG, "Requesting MQTT credentials...");
    esp_http_client_config_t config = {
        .url = "https://howsmyssl.com",
        .crt_bundle_attach = esp_crt_bundle_attach, // Uses built-in certificate bundle
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "HTTPS Status = %d, content_length = %lld\n",
                 esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client));
    }
    else
    {
        ESP_LOGE(TAG, "Error perform HTTPS request %s\n", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}
