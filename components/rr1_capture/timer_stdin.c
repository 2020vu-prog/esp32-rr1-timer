#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "timer_stdin.h"
// #include "freertos/task.h"

#include <stdio.h>
#include "esp_log.h"
#include "esp_system.h"

static const char *TAG = "timer_stdin";

void readNdJson(void *pvParameter)
{
	char accum[128] = "";
	char buf[128];
	while (1)
	{
		if (fgets(buf, 128, stdin))
		{
			ESP_LOGE(TAG, "read Chars: %s", buf);
			strncat(accum, buf, sizeof(accum) - strlen(accum) - 1);
			if (strchr(buf, '}') != NULL)
			{
				ESP_LOGE(TAG, "readNdJson: %s", accum);
				// Process the received line
				accum[0] = '\0'; // reset buffer
			}
			if (strlen(accum) > 100)
			{
				ESP_LOGE(TAG, "readNdJson: buffer overflow protection triggered");
				accum[0] = '\0'; // reset buffer
			}
		}
		vTaskDelay(100 / portTICK_PERIOD_MS);
	}
}

void init_timer_stdin()
{
	ESP_LOGI(TAG, "init_timer_stdin: BEGIN");
	xTaskCreate(&readNdJson, "readNdJson", 8192, NULL, 5, NULL);
}