/*
Copyright (c) 2017-2019 Tony Pottier

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

@file main.c
@author Tony Pottier
@brief Entry point for the ESP32 application.
@see https://idyl.io
@see https://github.com/tonyp7/esp32-wifi-manager
*/

#include "esp_timer.h"
#include <stdio.h>
#include <string.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"

#include <driver/gptimer.h>
// tonyp #include "wifi_manager.h"
// #include "wifi_config.h"
#include <inttypes.h>
// #include "esp32_perfmon.h"
#include "timer_capture.h"
#include "timer_mqtt.h"

#include "quad_uint32.h"

/* @brief tag used for ESP serial console messages */
static const char TAG[] = "main";

/**
 * @brief this is an exemple of a callback that you can setup in your own app to get notified of wifi manager event.
 */
void cb_connection_ok(void *pvParameter)
{
	ip_event_got_ip_t *param = (ip_event_got_ip_t *)pvParameter;

	/* transform IP to human readable string */
	char str_ip[16];
	esp_ip4addr_ntoa(&param->ip_info.ip, str_ip, IP4ADDR_STRLEN_MAX);

	ESP_LOGI(TAG, "I have a connection and my IP is %s!", str_ip);
	mqtt_app_start();
}

void XXapp_main()
{

	// wifi_config_init2("my-accessory", "my-password", on_wifi_event);
	/* your code should go here. Here we simply create a task on core 2 that monitors free heap memory */
	//  xTaskCreatePinnedToCore(&monitoring_task, "monitoring_task1", 2048, "t1", 1, NULL, 1);
	// xTaskCreatePinnedToCore(&monitoring_task, "monitoring_task0", 2048, "t0", 1, NULL, 0);

	// xTaskCreatePinnedToCore(&alarm_task, "alarm_task1", 4096, "t1", 1, NULL, 1);
	// xTaskCreatePinnedToCore(&dump_task, "dump_task0", 4096, "t0", 1, NULL, 0);
	//  perfmon_start();
	testem();
	capture_main();
}
