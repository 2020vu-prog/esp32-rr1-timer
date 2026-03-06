#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_private/esp_clk.h"
#include "driver/mcpwm_cap.h"
#include "driver/gpio.h"

#include "esp_timer.h"
#include "esp_check.h"
#include "timer_hist.h"
#include "timer_mqtt.h"
#include "timer_capture.h"
#include "quad_uint32.h"
#include "gps_xlate.h"

#include <math.h>
#include <time.h>
#include <uuid.h>

const static char *TAG = "rr1_capture";

qcontrol_handle *quad_h;
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Please update the following configuration according to your board spec ////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void pinHandlerLane(esp_probe_recv_data_t *);
void pinHandlerGps(esp_probe_recv_data_t *);

pindef_t pindefs[] = {

    // BEWARE hardcoded gpsPindef index 0!!
    {
	    gpio : GPS_PPS_GPIO,
	    pname : "gps_pps",
	    pull_down : true,
	    neg_edge : true,
	    pinHandlerFunc : pinHandlerGps,

    },
    {
	    gpio : LANE1_GPIO,
	    lane_index : 0,
	    pname : "lane1",
	    pinHandlerFunc : pinHandlerLane,
    },
    {
	    gpio : LANE2_GPIO,
	    lane_index : 1,
	    pname : "lane2",
	    pinHandlerFunc : pinHandlerLane,
    },
};
pindef_t *gpsPindef = &pindefs[0];

#define ESP_PROBE_DEFAULT_Q_DEPTH 25
#define ESP_PROBE_ALLOC_CAPS (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
#define ESP_PROBE_ALLOC_SPI (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

volatile int recv_que_overflow;
;
QueueHandle_t recv_que;
void apply64bitHysterisis(esp_probe_recv_data_t *);

IRAM_ATTR static bool mcpwm_timer_isr_callback(mcpwm_cap_channel_handle_t cap_chan, const mcpwm_capture_event_data_t *edata, void *user_data)
{
	/*
	static uint32_t cap_val_begin_of_sample = 0;
	static uint32_t cap_val_end_of_sample = 0;
	TaskHandle_t task_to_notify = (TaskHandle_t)user_data;
	BaseType_t high_task_wakeup = pdFALSE;

	// calculate the interval in the ISR,
	// so that the interval will be always correct even when capture_queue is not handled in time and overflow.
	if (edata->cap_edge == MCPWM_CAP_EDGE_POS)
	{
	    // store the timestamp when pos edge is detected
	    cap_val_begin_of_sample = edata->cap_value;
	    cap_val_end_of_sample = cap_val_begin_of_sample;
	}
	else
	{
	    cap_val_end_of_sample = edata->cap_value;
	    uint32_t tof_ticks = cap_val_end_of_sample - cap_val_begin_of_sample;

	    // notify the task to calculate the distance
	    xTaskNotifyFromISR(task_to_notify, tof_ticks, eSetValueWithOverwrite, &high_task_wakeup);
	}

	return high_task_wakeup == pdTRUE;
	*/

	// static DRAM_ATTR uint32_t prior_value32 = 0;
	// static DRAM_ATTR uint64_t wrap_count32 = 0;
	// if (prior_value32 > edata->cap_value)
	//{
	//    wrap_count32++;
	//}
	// prior_value32 = edata->cap_value;
	// uint64_t cap_value64 = ((uint64_t)(edata->cap_value)) | (wrap_count32 << 32);

	esp_probe_recv_data_t recv_data = {
	    // 80mhz,overflows approx 1x/minute
	    // just send 32 bit val..  enhance after isr
	    .cap_value64 = (uint64_t)(edata->cap_value),
	    // .cap_value64 = cap_value64,
	    .cap_edge = edata->cap_edge,
	    .pin_user_data = user_data,
	};
	BaseType_t need_yield;
	// per freertos: Items are queued by copy not reference
	int rc = xQueueSendFromISR(recv_que, &recv_data, &need_yield);
	if (rc != pdPASS)
	{
		recv_que_overflow++;
	}
	// ESP_LOGI(TAG, "ISR: %d\n", (int)edata->recv_bytes);

	return need_yield == pdTRUE;
}

mcpwm_cap_channel_handle_t capture_channel_setup(pindef_t *pd, mcpwm_cap_timer_handle_t cap_timer)
{
	ESP_LOGI(TAG, "Install capture channel %s [%d]", pd->pname, pd->gpio);
	mcpwm_cap_channel_handle_t cap_local_chan_h = NULL;
	mcpwm_capture_channel_config_t cap_ch_conf = {
	    .gpio_num = pd->gpio,
	    .prescale = 1,
	    // capture on both edge
	    .flags.neg_edge = pd->neg_edge,
	    .flags.pos_edge = pd->pos_edge,
	    // pull up internally
	    .flags.pull_down = pd->pull_down,
	    .flags.pull_up = pd->pull_up,
	};
	ESP_ERROR_CHECK(mcpwm_new_capture_channel(cap_timer, &cap_ch_conf, &cap_local_chan_h));

	ESP_LOGI(TAG, "Register capture callback");
	//  TaskHandle_t cur_task = xTaskGetCurrentTaskHandle();
	mcpwm_capture_event_callbacks_t cbs = {
	    .on_cap = mcpwm_timer_isr_callback,
	};

	ESP_ERROR_CHECK(mcpwm_capture_channel_register_event_callbacks(cap_local_chan_h, &cbs, pd));

	ESP_LOGI(TAG, "Enable capture channel");
	ESP_ERROR_CHECK(mcpwm_capture_channel_enable(cap_local_chan_h));
	pd->channel_h = cap_local_chan_h;
	return cap_local_chan_h;
}
static mcpwm_cap_timer_handle_t gcap_timer = NULL;

esp_err_t capture_setup(void)
{
	timer_hist_init();
	esp_err_t ret = ESP_OK;

	// Create the receive queue
	recv_que = xQueueCreateWithCaps(ESP_PROBE_DEFAULT_Q_DEPTH, sizeof(esp_probe_recv_data_t), ESP_PROBE_ALLOC_CAPS);
	ESP_GOTO_ON_FALSE(recv_que, ESP_ERR_NO_MEM, err, TAG, "no memory for receive queue");

	mcpwm_cap_timer_handle_t cap_timer = NULL;
	mcpwm_capture_timer_config_t cap_conf = {
	    .clk_src = MCPWM_CAPTURE_CLK_SRC_DEFAULT,
	    .group_id = 0,
	};
	ESP_ERROR_CHECK(mcpwm_new_capture_timer(&cap_conf, &cap_timer));

	// Calculate the total size of the array in bytes
	size_t array_size_bytes = sizeof(pindefs);
	// Calculate the size of a single element in bytes
	size_t element_size_bytes = sizeof(pindefs[0]);
	// Calculate the number of elements
	size_t array_length = array_size_bytes / element_size_bytes;
	for (int x = 0; x < array_length; x++)
	{
		capture_channel_setup(&pindefs[x], cap_timer);
	}

	ESP_LOGI(TAG, "Enable and start capture timer");
	ESP_ERROR_CHECK(mcpwm_capture_timer_enable(cap_timer));
	ESP_ERROR_CHECK(mcpwm_capture_timer_start(cap_timer));
	gcap_timer = cap_timer;
	return ret;
err:
	return ret;
}
static uint64_t softGpsTicks = 0;

void mqHealth(void)
{
	static uint64_t lastUs = 0;
	uint64_t upUs = esp_timer_get_time();
	if (upUs > lastUs + 30000000)
	{
		mq_pub("health30");
		lastUs = upUs;
	}
}
void capture_main(void)
{

	quad_h = init_qcontrol();
	capture_setup();
	ESP_LOGI(TAG, "Install capture timer");

	uuid_set_mode(UUID_MODE_RANDOM);
	uuid_set_mode(UUID_MODE_VARIANT4);

	uuid_init();
	const char *uuid_ran = uuid_generate();

	uint64_t priorv = 0;
	while (1)
	{
		mqHealth();
		ESP_LOGI(TAG, "xQueueReceive: top");
		ESP_LOGI(TAG, "Generated UUID: %s", uuid_ran);
		// wait for echo done signal
		/// uint32_t timer_value = mcpwm_capture_timer_get_value(gcap_timer);
		// uint32_t timer_value = mcpwm_capture_signal_get_value(gcap_timer);
		uint32_t timer_value = 17;
		char buf[128] = {};
		snprintf(buf, 128, "%s %ld", TAG, timer_value);

		// mq_pub(buf);

		esp_probe_recv_data_t recv_data = {};
		if (xQueueReceive(recv_que, &recv_data, pdMS_TO_TICKS(10000)) == pdTRUE)
		{
			apply64bitHysterisis(&recv_data);
			uint64_t elapsed = recv_data.cap_value64 - priorv;
			// pindef_t *pd = (pindef_t *)recv_data.pin_user_data;
			pindef_t *pd = recv_data.pin_user_data;
			ESP_LOGI(TAG, "xQueueReceive got: %" PRIu64 " %d %d %s",
				 recv_data.cap_value64,
				 (int)pd->gpio,
				 (int)recv_data.cap_edge,
				 pd->pname);
			snprintf(buf, 128, "gpio: %" PRIu64 " %s %d E:%" PRIu64,
				 recv_data.cap_value64,
				 pd->pname,
				 (int)recv_data.cap_edge,
				 elapsed);
			if (recv_data.cap_edge) // wip
			{

				priorv = recv_data.cap_value64;
				mq_pub(buf);
			}
			if (pd && pd->pinHandlerFunc)
			{
				pd->pinHandlerFunc(&recv_data);
			}
			/*
			{
			    float pulse_width_us = tof_ticks * (1000000.0 / esp_clk_apb_freq());
			    if (pulse_width_us > 35000)
			    {
				// out of range
				continue;
			    }
			    // convert the pulse width into measure distance
			    float distance = (float)pulse_width_us / 58;
			    ESP_LOGI(TAG, "Measured distance: %.2fcm", distance);
			}
			vTaskDelay(pdMS_TO_TICKS(500));
			*/
		}
		else
		{

			softGpsTicks = esp_timer_get_time();
			mcpwm_capture_channel_trigger_soft_catch(gpsPindef->channel_h);
		}
	}
}

void apply64bitHysterisis(esp_probe_recv_data_t *recv_dataP)
{

	recv_dataP->cap_value64 &= 0x00000000FFFFFFFF;
	recv_dataP->cap_value64 = qc_get64(quad_h, recv_dataP->cap_value64);
}
/*
 calculate high order bits, based on CBU up time.
  assumes this will be called shortly after the time stamp is captured
*/
void apply64bitHysterisisOLD(esp_probe_recv_data_t *recv_dataP)
{
	// turn off high  bits
	recv_dataP->cap_value64 &= 0x00000000FFFFFFFF;

	// u_int64_t upUs = esp_timer_get_time();
	uint64_t up80mhzTicks = esp_timer_get_time() * 80;
	long double wraps = up80mhzTicks - recv_dataP->cap_value64; //
	wraps = wraps / UINT32_MAX;

	ESP_LOGI(TAG, "apply64bitHysterisis : %.8f", wraps);
	uint64_t wrap64 = roundl(wraps);
	ESP_LOGI(TAG, "apply64bitHysterisis 64: %" PRIu64 " ", wrap64);
	recv_dataP->cap_value64 |= (wrap64 << 32);
	// uint64_t cap_value64 = ((uint64_t)(edata->cap_value)) | (wrap_count32 << 32);
}

void pinHandlerLane(esp_probe_recv_data_t *recv_dataP)
{
	th_append(recv_dataP);
}
// real gps will suppress q timeout and therefore soft gps
bool isSoftGps()
{
	uint64_t nowGpsTicks = esp_timer_get_time();
	if (nowGpsTicks - softGpsTicks > 10000000)
	{
		return false;
	}
	return true;
}
void pinHandlerGps(esp_probe_recv_data_t *recv_dataP)
{
	if (isSoftGps())
	{
		ESP_LOGI(TAG, "pinHandlerGps : skipping soft");

		return;
	}
	log_gps_pps(recv_dataP);
}
