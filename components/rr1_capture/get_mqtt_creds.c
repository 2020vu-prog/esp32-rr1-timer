#include "esp_http_client.h"
#include "esp_crt_bundle.h"

#include "get_mqtt_creds.h"
#include "timer_mqtt.h"
#include "esp_log.h"
#include <cJSON.h>
#include "rr1_wifi.h"

#define BUFLEN 10000
const static char *TAG = "get_mqtt_creds";
typedef struct _rr1_creds
{
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
void free_creds()
{
	if (creds.authUrl)
	{
		free(creds.authUrl);
		creds.authUrl = NULL;
	}
	if (creds.mqtt_host)
	{
		free(creds.mqtt_host);
		creds.mqtt_host = NULL;
	}

	if (creds.mqtt_cert)
	{
		free(creds.mqtt_cert);
		creds.mqtt_cert = NULL;
	}
	if (creds.mqtt_key)
	{
		free(creds.mqtt_key);
		creds.mqtt_key = NULL;
	}
}
void malloc_and_strcpy(char **dest, const char *src)
{
	if (src == NULL)
	{
		free(*dest);
		*dest = NULL;
		return;
	}
	size_t len = strlen(src) + 1;
	*dest = malloc(len);
	if (*dest != NULL)
	{
		strncpy(*dest, src, len);
	}
	else
	{
		ESP_LOGE(TAG, "Failed to allocate memory for string copy");
	}
}
void copyJsonString(const cJSON *jsonObj, char **dest, const char *key)
{
	const cJSON *jsonMember = NULL;

	jsonMember = cJSON_GetObjectItemCaseSensitive(jsonObj, key);

	if (cJSON_IsString(jsonMember) && (jsonMember->valuestring != NULL))
	{
		ESP_LOGI(TAG, "Checking [%s] jsonStr \"%s\"\n", key, jsonMember->valuestring);
		malloc_and_strcpy(dest, jsonMember->valuestring);
	}
	else
	{
		ESP_LOGW(TAG, "Key [%s] not found or not a string in JSON\n", key);
	}
}
int parse_authApiKey(const char *const jsonStr)
{
	int status = 0;
	cJSON *jsonObj = cJSON_Parse(jsonStr);
	if (jsonObj == NULL)
	{
		const char *error_ptr = cJSON_GetErrorPtr();
		if (error_ptr != NULL)
		{
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

int parse_discover(const char *const jsonStr)
{
	int status = 0;
	cJSON *jsonObj = cJSON_Parse(jsonStr);
	if (jsonObj == NULL)
	{
		const char *error_ptr = cJSON_GetErrorPtr();
		if (error_ptr != NULL)
		{
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

esp_err_t accum_event_handler(esp_http_client_event_t *evt)
{
	static int buf_used = 0;
	if (evt == NULL || evt->user_data == NULL)
	{
		buf_used = 0;

		ESP_LOGE(TAG, "Invalid event data");
		return ESP_FAIL;
	}
	switch (evt->event_id)
	{
	case HTTP_EVENT_ON_DATA:
		// Check if there is data and it's not a chunked response header
		if (!esp_http_client_is_chunked_response(evt->client))
		{
			ESP_LOGI(TAG, "Chunk received: %.*s\n", evt->data_len, (char *)evt->data);
			strncat((char *)evt->user_data, (char *)evt->data, MIN(evt->data_len, BUFLEN - buf_used));
			buf_used += evt->data_len;
		}
		else
		{
			ESP_LOGI(TAG, "Chunk header received");
		}
		break;
	// Handle other events like HTTP_EVENT_ERROR or HTTP_EVENT_ON_FINISH
	default:
		break;
	}
	return ESP_OK;
}
void https_request_creds(void)
{
	char host_name[12];
	get_device_hostname(host_name, 12);

	https_request_discover(host_name);
	https_request_auth(host_name);
}
void https_request_auth(char *host_name)
{

	char *buffer = malloc(BUFLEN + 1);
	memset(buffer, 0, BUFLEN + 1);

	char authUrl[512];
	snprintf(authUrl, sizeof(authUrl), "%s", creds.authUrl);
	char *authPath = strstr(authUrl, "/auth");
	if (authPath)
	{
		//*authPath = '\0'; // Terminate the string at the start of "/auth"
		strcpy(authPath, "/authApiKey");
	}
	// 1. Create JSON payload
	cJSON *root = cJSON_CreateObject();
	cJSON_AddStringToObject(root, "TIMER", host_name);
	char apikey[256]="";
	nvs_get_rr1_apikey(apikey, sizeof(apikey));
	ESP_LOGI(TAG, "NVS returned apiKey [%s]", apikey);
	cJSON_AddStringToObject(root, "apiKey", apikey);

	char *post_data = cJSON_PrintUnformatted(root);

	// 2. HTTP Client Configuration
	accum_event_handler(NULL); // Reset static buffer index

	esp_http_client_config_t config = {
	    //.url = "http://your-api-endpoint.com",
	    .method = HTTP_METHOD_POST,

	    .url = authUrl,
	    .crt_bundle_attach = esp_crt_bundle_attach, // Uses built-in certificate bundle
	    .transport_type = HTTP_TRANSPORT_OVER_SSL,
	    .user_data = buffer, // Buffer to store response
	    .event_handler = accum_event_handler,

	};
	esp_http_client_handle_t client = esp_http_client_init(&config);

	// 3. Set Header and Post Data
	esp_http_client_set_header(client, "Content-Type", "application/json");
	esp_http_client_set_post_field(client, post_data, strlen(post_data));

	// 4. Perform Request
	esp_err_t err = esp_http_client_perform(client);
	if (err == ESP_OK)
	{
		ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %lld",
			 esp_http_client_get_status_code(client),
			 esp_http_client_get_content_length(client));
		ESP_LOGI(TAG, "POST Received datalen: %d", strlen(buffer));
		ESP_LOGI(TAG, "POST Received data: %s", buffer);

		parse_authApiKey(buffer);
	}
	else
	{
		ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
	}

	// 5. Cleanup
	esp_http_client_cleanup(client);
	cJSON_Delete(root);
	free(post_data);
	free(buffer);
}

void https_request_discover(char *host_name)
{
	ESP_LOGI(TAG, "Requesting MQTT credentials...237");

	char *buffer = malloc(BUFLEN + 1);
	memset(buffer, 0, BUFLEN + 1);

	esp_http_client_config_t config = {
	    .url = "https://test.rr1.us/app/iot/discover",
	    .crt_bundle_attach = esp_crt_bundle_attach, // Uses built-in certificate bundle
	    .transport_type = HTTP_TRANSPORT_OVER_SSL,
	    .user_data = buffer, // Buffer to store response
	    .event_handler = accum_event_handler,

	};

	esp_http_client_handle_t client = esp_http_client_init(&config);
	esp_http_client_set_header(client, "x-rr1-timer", host_name);
	esp_http_client_set_method(client, HTTP_METHOD_GET);
	esp_err_t err = esp_http_client_perform(client);

	if (err == ESP_OK)
	{
		int clen = esp_http_client_get_content_length(client);

		ESP_LOGI(TAG, "HTTPS Status = %d, content_length = %d\n",
			 esp_http_client_get_status_code(client),
			 clen);
		if (buffer)
		{
			ESP_LOGI(TAG, "Received data: %s", buffer);
			parse_discover(buffer);
		}
		else
		{
			ESP_LOGE(TAG, "Failed to read response");
		}
	}

	else
	{
		ESP_LOGE(TAG, "Error perform HTTPS request %s\n", esp_err_to_name(err));
	}
	esp_http_client_cleanup(client);

	if (buffer)
		free(buffer);
}

const char *get_mqtt_host()
{
	return creds.mqtt_host;
}
const char *get_mqtt_cert()
{
	return creds.mqtt_cert;
}
const char *get_mqtt_key()
{
	return creds.mqtt_key;
}
