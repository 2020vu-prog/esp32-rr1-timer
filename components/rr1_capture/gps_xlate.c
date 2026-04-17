
#include "gps_xlate.h"
#include "timer_mqtt.h"
#include <time.h>
#include <math.h>
#include <string.h>
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define gps_list_max 8
#define MEG1 1000000

gps_xref gps_list[gps_list_max];
static int recent_pps = 0;
void log_gps_pps(esp_probe_recv_data_t *rd)
{
	recent_pps++;

	if (recent_pps >= gps_list_max)
	{
		recent_pps = 0;
	}
	double now = epoch_double();
	gps_list[recent_pps].epoch = round(now);
	gps_list[recent_pps].cap_value64 = rd->cap_value64;
};
const int YEAR5 = 3600 * 24 * 360 * 5;
bool isValid(gps_xref *t)
{
	return t->epoch > YEAR5;
}
int64_t xlateCap64(gps_xlate_handle_t *ghandle, uint64_t *sample64, struct timespec *resulTs)
{
	resulTs->tv_sec = 0;
	resulTs->tv_nsec = 0;

	if (ghandle && isValid(&ghandle->t0) && isValid(&ghandle->t1))
	{
		// ESP_LOGI("xlateCap64", " valid");
	}
	else
	{
		ESP_LOGI("xlateCap64", " invalid ghandle");
		return 0;
	}

	int64_t sampleOffset = *sample64 - ghandle->t0.cap_value64;
	// ESP_LOGI("xlateCap64", "sampleOffset %" PRId64, sampleOffset);

	int64_t epoch64us = (sampleOffset * ghandle->itDelta * MEG1) / ghandle->igDelta;
	epoch64us += (ghandle->t0.epoch * MEG1);
	/*
	float epochOffsetSeconds = (sampleOffset * ghandle->ratioF);
	*/
	resulTs->tv_sec = epoch64us / MEG1;
	resulTs->tv_nsec = (epoch64us % MEG1) * 1000L;
	return epoch64us;
}
void init_ghandle(gps_xlate_handle_t *ghandle)
{
	int64_t igDelta = ghandle->t1.cap_value64 - ghandle->t0.cap_value64;
	int64_t itDelta = ghandle->t1.epoch - ghandle->t0.epoch;
	ESP_LOGI("init_ghandle", "ig %" PRId64, igDelta);
	ESP_LOGI("init_ghandle", "it %" PRId64, itDelta);
	ghandle->igDelta = igDelta;
	ghandle->itDelta = itDelta;
	/*
	    float gDeltaF = igDelta;
	    float tDeltaF = itDelta;
	    ESP_LOGI("init_ghandle", "g float %f", gDeltaF);
	    ESP_LOGI("init_ghandle", "t float %f", tDeltaF);
	    ghandle->ratioF = tDeltaF / gDeltaF;
	    ESP_LOGI("init_ghandle", "ratio float %f", ghandle->ratioF);
	*/
	/*
	    long double gDelta = ghandle->t0.cap_value64 - ghandle->t1.cap_value64;
	    long double tDelta = ghandle->t0.epoch - ghandle->t1.epoch;
	    ESP_LOGI("init_ghandle", "g FOO %Lf", gDelta);
	    ESP_LOGI("init_ghandle", "t FOO %Lf", tDelta);
	    gDelta = igDelta;
	    tDelta = itDelta;
	    ESP_LOGI("init_ghandle", "g Lfi %Lf", gDelta);
	    ESP_LOGI("init_ghandle", "t Lfi %Lf", tDelta);
	    ghandle->ratioLdSlow = tDelta / gDelta;
	    ESP_LOGI("init_ghandle",  "ratio   Lfi %Lf", ghandle->ratioLdSlow);
	*/
}
// poopulate caller's struct!
void getGpsHandle(gps_xlate_handle_t *ghandle)
{
	int oldPps = recent_pps + 1;
	if (oldPps >= gps_list_max)
	{
		oldPps = 0;
	}
	memset(ghandle, 0, sizeof(*ghandle));
	ghandle->t0 = gps_list[recent_pps];
	ghandle->t1 = gps_list[oldPps];

	int delta = ghandle->t0.epoch - ghandle->t1.epoch;
	ESP_LOGI("getGpsHandle", "delta %d", delta);

	if (delta != (gps_list_max - 1))
	{
		memset(ghandle, 0, sizeof(*ghandle));
		return;
	}
	init_ghandle(ghandle);
	return;
}

#define TST_XLATE_LO (50 * MEG1)
#define TST_XLATE_HI ((51 * MEG1) + 30)
#define TST_EDGE 70000
void test_ghandle(void)
{

	gps_xlate_handle_t testHandle = {
	    .t0 = {
		.epoch = (7) + YEAR5,
		.cap_value64 = TST_XLATE_LO,
	    },
	    .t1 = {
		.epoch = (9) + YEAR5,
		.cap_value64 = TST_XLATE_HI,
	    },
	    //.ratio = 9.9, // to be calculated
	};
	gps_xlate_handle_t *ghandle = &testHandle;
	init_ghandle(&testHandle);
	ESP_LOGI("test_ghandle", "t0 epoch %" PRId64 " capv %" PRId64, ghandle->t0.epoch, ghandle->t0.cap_value64);
	ESP_LOGI("test_ghandle", "t0 epoch int %d ", (int)ghandle->t0.epoch);
	ESP_LOGI("test_ghandle", "t0 epoch z %d ", sizeof(ghandle->t0.epoch));
	ESP_LOGI("test_ghandle", "t0 capv %" PRId64, ghandle->t0.cap_value64);
	ESP_LOGI("test_ghandle", "t1 epoch %" PRId64 " capv %" PRId64, ghandle->t1.epoch, ghandle->t1.cap_value64);
	// ESP_LOGI("test_ghandle", "ratio Lfi %Lf", ghandle->ratioLdSlow);
	// ESP_LOGI("test_ghandle", "ratio float %f", ghandle->ratioF);

	struct timespec resulTs;

	uint64_t sample64;
	int64_t resultUs;

	sample64 = TST_XLATE_LO;
	resultUs = xlateCap64(ghandle, &sample64, &resulTs);
	ESP_LOGI("test_ghandle", "sample64 %" PRIu64 " xlated i %" PRId64, sample64, resultUs);

	sample64 = TST_XLATE_HI;
	resultUs = xlateCap64(ghandle, &sample64, &resulTs);
	ESP_LOGI("test_ghandle", "sample64 %" PRIu64 " xlated i %" PRId64, sample64, resultUs);

	int deltaMin = 100000000;
	int deltaMax = 0;

	int64_t priorUs = -1;
	for (sample64 = TST_XLATE_LO - TST_EDGE; sample64 <= TST_XLATE_HI + TST_EDGE; sample64 += 10000)
	{
		taskYIELD();
		vTaskDelay(pdMS_TO_TICKS(1));
		resultUs = xlateCap64(ghandle, &sample64, &resulTs);
		if (priorUs != -1)
		{
			int delta = resultUs - priorUs;
			if (delta < deltaMin)
			{
				deltaMin = delta;
			}
			if (delta > deltaMax)
			{
				deltaMax = delta;
			}
			ESP_LOGI("test_ghandle", "sample64 %" PRIu64 " xlated i %" PRId64 " delta %d", sample64, resultUs, delta);
		}
		priorUs = resultUs;
	}
	ESP_LOGI("test_ghandle", "deltaMin %d deltaMax %d", deltaMin, deltaMax);
	/*
	sample64 = 50 * MEG1 + 500015;
	resultUs = xlateCap64(ghandle, &sample64, &resulTs);
	ESP_LOGI("test_ghandle", "sample64 %" PRIu64 " xlated i %" PRId64, sample64, resultUs);

	sample64 = 50 * MEG1 + 500000;
	resultUs = xlateCap64(ghandle, &sample64, &resulTs);
	ESP_LOGI("test_ghandle", "sample64 %" PRIu64 " xlated i %" PRId64, sample64, resultUs);
    */
}
