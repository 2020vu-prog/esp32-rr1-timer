#include "timer_hist.h"
#include "stddef.h"
#include <string.h>
#include "gps_xlate.h"
#include "mbedtls/base64.h"

#include "esp_heap_caps.h"
#include "timer.pb-c.h"
#include "timer_health.h"
#include "timer_mqtt.h"
#include "aws-bandaid.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "rr1_wifi.h"

Timerpb__TimerData *marshalRr1TimerPbTimerDataHealth();
const static char *TAG = "rr1_capture";
timer_config_t timerConfig = {
	clearMs : 10 * 1000,
	maxCarMs : 500,
	minCarMs : 50,
	maxPerfs : 2,

};
candidate_block_t candidateBlock = {
	expiryTicks64 : 0,
	birthTicks64 : 0,
	priorState : {},
};
lane_transition_t *hist;
lane_transition_t recentState[2] = {};
int nextHist = 0;
// int recentHist = 0;
int nextXmitHist = 0;

inline int dec_hist(int h)
{
	return (h - 1) & HIST_MAX;
}
inline int inc_hist(int h)
{

	return (h + 1) & HIST_MAX;
}

void timer_hist_init()
{
	ESP_LOGI(TAG, "timer_hist_init: BEGIN");

	heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);
	size_t size = sizeof(lane_transition_t) * (HIST_MAX + 1);
	hist = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
	ESP_LOGI(TAG, "timer_hist_init: %d :: %p ", size, hist);
	memset(hist, 0, size);
	test_ghandle();
	ESP_LOGI(TAG, "timer_hist_init: END");
}
uint64_t msecsToTicks(uint64_t ms)
{
	return ms * 1000;
}
void potentialRollCandidate(lane_transition_t *hp)
{
	// lane_transition_t *hp = &hist[recentHist];
	if (candidateBlock.expiryTicks64 < hp->cap_value64)
	{
		memset(&candidateBlock, 0, sizeof(candidateBlock));

		candidateBlock.birthIndex = dec_hist(nextHist);
		candidateBlock.birthTicks64 = hp->cap_value64;
		candidateBlock.expiryTicks64 = candidateBlock.birthTicks64 + msecsToTicks(timerConfig.clearMs);
		candidateBlock.priorState[0] = recentState[0];
		candidateBlock.priorState[1] = recentState[1];
		getGpsHandle(&candidateBlock.ghandle);
		candidateBlock.lastAudit = NULL;
		candidateBlock.auditPending = true;
	}
	struct timespec resulTs;
	hp->gps_micros = xlateCap64(&candidateBlock.ghandle, &hp->cap_value64, &resulTs);
	recentState[hp->lane_index] = *hp;
}
void process_lane_transition(lane_transition_t *hp, lane_finish_t *lf)
{
	if (!lf->nose || hp->cap_value64 < lf->nose->cap_value64)
	{
		lf->nose = hp;
	}
	if (!lf->tail || hp->cap_value64 > lf->tail->cap_value64)
	{
		lf->tail = hp;
	}
	lf->transition_count += 1;
}

void lfError(lane_finish_t *lf, char errCode)
{
	lf->errs[0] = errCode;
}
void auditFinish(lane_finish_t *lf)
{
	if (!lf->nose)
	{
		lfError(lf, FE_MISSING_NOSE);
		return;
	}
	if (!lf->tail)
	{
		lfError(lf, FE_MISSING_TAIL);
		return;
	}
	int carLenTicks = lf->tail->cap_value64 - lf->nose->cap_value64;
	if (carLenTicks > msecsToTicks(timerConfig.maxCarMs))
	{
		lfError(lf, FE_MAXCARLEN);
		return;
	}
	if (carLenTicks < msecsToTicks(timerConfig.minCarMs))
	{
		lfError(lf, FE_MINCARLEN);
		return;
	}
	// no perfs s/b 2 transitions (nose, tail)
	// one perfs s/b 4 transitions (nose, beginPerf,endPerf, tail)
	if (lf->transition_count > (timerConfig.maxPerfs + 1) * 2)
	{
		lfError(lf, FE_PERFCOUNT);
		return;
	}
	lfError(lf, FE_NONE);
	return;
}
void auditCandidateHist()
{
	lane_finish_t l12[2] = {};
	//    lane_finish_t l2 = {};

	for (int x = dec_hist(nextHist); hist[x].cap_value64 >= candidateBlock.birthTicks64; x = dec_hist(x))
	{
		ESP_LOGI(TAG, "auditCandidateHist %d %d", x, HIST_MAX);
		lane_transition_t *hp = &hist[x];
		process_lane_transition(hp, &l12[hp->lane_index]);
	}
	for (int j = 0; j < 2; j++)
	{
		auditFinish(&l12[j]);
	}
	// TODO:  do something with finish
	candidateBlock.auditPending = false;
}
void th_append(esp_probe_recv_data_t *recv_dataP)
{
	if (!hist)
	{
		ESP_LOGI(TAG, "th_append: SKIPPED no mem");
		return;
	}
	lane_transition_t *hpNext = &hist[nextHist];

	hpNext->gps_micros = 0; //  defer until candidate block is assigned
	hpNext->lane_result_state = getResultState(recv_dataP->cap_edge);
	hpNext->cap_value64 = recv_dataP->cap_value64;
	hpNext->lane_gpio = recv_dataP->pin_user_data->gpio;
	hpNext->lane_index = recv_dataP->pin_user_data->lane_index;
	ESP_LOGI(TAG, "th_append gpio:%d state: %d",
		 (int)hpNext->lane_gpio,
		 (int)hpNext->lane_result_state);

	nextHist = inc_hist(nextHist);
	potentialRollCandidate(hpNext);
	// VERY SLOW
	// auditCandidateHist();
	candidateBlock.auditPending = true;
}
lane_state_enum getResultState(mcpwm_capture_edge_t cap_edge)
{
	if (isLaneClear(cap_edge))
	{
		return LANE_CLEAR;
	}
	else
	{
		return LANE_BLOCKED;
	}
}
bool isLaneClear(mcpwm_capture_edge_t cap_edge)
{
	return cap_edge == MCPWM_CAP_EDGE_POS;
}

Timerpb__TimerData *marshalRr1TimerPbTimerData(lane_transition_t *h)
{
	Timerpb__TimerData *td = malloc(sizeof(Timerpb__TimerData));
	;
	timerpb__timer_data__init(td);

	Timerpb__TimerPin *tp = malloc(sizeof(Timerpb__TimerPin));
	;
	timerpb__timer_pin__init(tp);

	td->timerpin = tp;

	tp->has_pinname = true;
	tp->pinname = h->lane_index == 0 ? TIMERPB__PIN_NAME__lane1 : TIMERPB__PIN_NAME__lane2;
	tp->has_pinstate = true;
	tp->pinstate = h->lane_result_state == LANE_CLEAR ? TIMERPB__PIN_STATE__CLEAR : TIMERPB__PIN_STATE__BLOCKED;
	tp->has_pinnumber = true;
	tp->pinnumber = h->lane_gpio;
	tp->stamp = malloc(sizeof(Timerpb__TimerTimeStamp));
	timerpb__timer_time_stamp__init(tp->stamp);

	tp->stamp->has_tick64 = true;
	tp->stamp->tick64 = h->cap_value64;

	if (h->gps_micros > 0)
	{
		tp->stamp->gpstime = malloc(sizeof(Google__Protobuf__Timestamp));
		google__protobuf__timestamp__init(tp->stamp->gpstime);

		tp->stamp->gpstime->seconds = h->gps_micros / MEG;
		tp->stamp->gpstime->nanos = (h->gps_micros % MEG) * 1000;
	}
	else
	{
		tp->stamp->gpstime = NULL;
	}
	return td;
}
void freeRr1TimerPbTimerData(Timerpb__TimerData *h)
{
	if (h->timerpin)
	{
		if (h->timerpin->stamp)
		{
			if (h->timerpin->stamp->gpstime)
			{
				free(h->timerpin->stamp->gpstime);
			}
			free(h->timerpin->stamp);
		}
		free(h->timerpin);
	}

	if (h->timerhealth)
	{
		if (h->timerhealth->stamp)
		{
			if (h->timerhealth->stamp->gpstime)
			{
				free(h->timerhealth->stamp->gpstime);
			}
			free(h->timerhealth->stamp);
		}
		if (h->timerhealth->wifiip)
		{
			free(h->timerhealth->wifiip);
		}
		if (h->timerhealth->wirelessmac)
		{
			free(h->timerhealth->wirelessmac);
		}
		if (h->timerhealth->ssid)
		{
			free(h->timerhealth->ssid);
		}
		free(h->timerhealth);
	}
}
void freeRr1TimerPbTimerDataList(Timerpb__TimerDataList *tdl)
{
	for (int x = 0; x < tdl->n_timerdata; x++)
	{
		freeRr1TimerPbTimerData(tdl->timerdata[x]);
		free(tdl->timerdata[x]);
	}
	free(tdl->timerdata);
	free(tdl);
}

int getXmitHistBacklog()
{
	int backlog = nextHist - nextXmitHist;
	return backlog & HIST_MAX;
}
Timerpb__TimerDataList *marshalRr1TimerPbTimerDataList(lane_transition_t *h, int *hCount);
int mqPubDataList()
{
	if(getMqttPublishCredits() < 1){
		ESP_LOGW(TAG, "mqPubDataList: no publish credits");
		return -1;
	}

	int ltCount = 0;
	Timerpb__TimerDataList *tdl = marshalRr1TimerPbTimerDataList(hist, &ltCount);
	if (!tdl)
	{
		ESP_LOGI(TAG, "mqPubDataList: nothing to publish");
		return 0;
	}
	size_t packed_size = timerpb__timer_data_list__get_packed_size(tdl);
	uint8_t *buffer = malloc(packed_size);
	timerpb__timer_data_list__pack(tdl, buffer);

	int rc = aba_xmit_b64_json(buffer, packed_size);

	free(buffer);
	freeRr1TimerPbTimerDataList(tdl);

	if (rc > 0)
	{
		nextXmitHist = (nextXmitHist + ltCount) & HIST_MAX;
		decrementMqttPublishCredits();
	}
	return rc;
}
int aba_xmit_b64_json(uint8_t *buffer, size_t packed_size)
{
	unsigned char *input = buffer;
	uint8_t *buffer64 = calloc(1, packed_size * 2);
	size_t outlen;

	mbedtls_base64_encode(buffer64, packed_size * 2, &outlen, input, packed_size);
	char *bj64 = aba_b64_json((char *)buffer64);

	int rc = mq_pub64(bj64);
	free(buffer64);
	free(bj64);
	return rc;
}
// TODO  Health is still due if, message send fails transmission
bool isHealthDue(int tlCount)
{
	static uint64_t lastHealthUs = 0;
	uint64_t upUs = esp_timer_get_time();
	int healthIntervalMs = tlCount > 0 ? 30000 : 55000; // if we have data to send, bundle health opportunistically
	if (upUs > lastHealthUs + (healthIntervalMs * 1000))
	{
		lastHealthUs = upUs;
		return true;
	}
	return false;
}
Timerpb__TimerDataList *marshalRr1TimerPbTimerDataList(lane_transition_t *h, int *tlUsed)
{

	int tlCount = getXmitHistBacklog();
	int healthCount = isHealthDue(tlCount) ? 1 : 0;
	if (tlCount > 20)
	{
		tlCount = 20; // cap the backlog to avoid creating huge messages
	}
	*tlUsed = tlCount;
	if (tlCount < 1 && healthCount < 1)
	{
		ESP_LOGI(TAG, "marshalRr1TimerPbTimerDataList: backlog %d empty", tlCount);
		return NULL;
	}
	Timerpb__TimerDataList *tdl = malloc(sizeof(Timerpb__TimerDataList));
	;
	timerpb__timer_data_list__init(tdl);

	tdl->n_timerdata = tlCount + healthCount;
	tdl->timerdata = malloc(sizeof(Timerpb__TimerData *) * tdl->n_timerdata);
	for (int x = 0; x < tlCount; x++)
	{
		int idx = (nextXmitHist + x) & HIST_MAX;
		h = &hist[idx];
		tdl->timerdata[x] = marshalRr1TimerPbTimerData(h);
		ESP_LOGI(TAG, "marshalRr1TimerPbTimerDataList: backlog %d idx %d ticks64 %" PRIu64, tlCount, idx, h->cap_value64);
	}
	if (healthCount)
	{
		tdl->timerdata[tlCount] = marshalRr1TimerPbTimerDataHealth();

		heap_caps_print_heap_info(MALLOC_CAP_8BIT);
	}


	struct timespec tv;
	if (clock_gettime(CLOCK_REALTIME, &tv))
	{
		perror("error clock_gettime\n");
	}
	else
	{
		tdl->has_xmitms = true;
		tdl->xmitms = tv.tv_sec * 1000 + (tv.tv_nsec / 1000000);
	}

	tdl->has_prevpubackms = true;
	tdl->prevpubackms = getMqttRecentLatencyMs();
	return tdl;
}
Timerpb__TimerData *marshalRr1TimerPbTimerDataHealth()
{

	ESP_LOGI(TAG, "marshalRr1TimerPbTimerDataHealth: ");

	Timerpb__TimerData *td = malloc(sizeof(Timerpb__TimerData));
	timerpb__timer_data__init(td);

	td->timerhealth = malloc(sizeof(Timerpb__TimerHealth));
	timerpb__timer_health__init(td->timerhealth);

	struct timespec tv;
	if (!clock_gettime(CLOCK_REALTIME, &tv))
	{
		td->timerhealth->stamp = malloc(sizeof(Timerpb__TimerTimeStamp));
		timerpb__timer_time_stamp__init(td->timerhealth->stamp);
		// to do: this is broken
		td->timerhealth->stamp->has_tick64 = true;
		td->timerhealth->stamp->tick64 = (uint64_t)tv.tv_sec * 1000000 + (tv.tv_nsec / 1000);
	}

	td->timerhealth->has_gpsemittingpps = true;
	td->timerhealth->gpsemittingpps = isGpsEmittingPps();

	td->timerhealth->has_ramfreekb = true;
	td->timerhealth->ramfreekb = esp_get_minimum_free_heap_size() / 1024;

	td->timerhealth->has_cputempc = true;
	td->timerhealth->cputempc = health_cpu_temp();

	td->timerhealth->has_cpuuptime = true;
	td->timerhealth->cpuuptime = esp_timer_get_time() / 1000000;

	td->timerhealth->ssid = malloc(40);
	get_wifi_ssid(td->timerhealth->ssid);

	td->timerhealth->has_mqttconnections = true;
	td->timerhealth->mqttconnections = getMqttConnectionCount();

	td->timerhealth->has_xmitcredits = true;
	td->timerhealth->xmitcredits = getMqttPublishCredits();
	
	td->timerhealth->has_wifirss = true;
	td->timerhealth->wifirss = getWifiRssi();

	td->timerhealth->has_maxpublishackms = true;
	td->timerhealth->maxpublishackms = getMqttMaxLatencyMs();

	td->timerhealth->has_gpsuptimecontiguous = true;
	td->timerhealth->gpsuptimecontiguous = getgpsUptimeContiguousSeconds();

	td->timerhealth->has_gpsflutter = true;
	td->timerhealth->gpsflutter = getGpsFlutter();

	if (wifi_ip[0])
	{
		td->timerhealth->wifiip = malloc(20);
		snprintf(td->timerhealth->wifiip, 20, "%s", wifi_ip);
	}

	td->timerhealth->wirelessmac = malloc(20);
	get_device_mac(td->timerhealth->wirelessmac, 20);

	td->timerhealth->has_gpsuptimetotal = true;
	td->timerhealth->gpsuptimetotal = getGpsUptimeTotalSeconds();

	td->timerhealth->has_gpsinitialacquisitionsecondsafterboot = true;
	td->timerhealth->gpsinitialacquisitionsecondsafterboot = getGpsInitialAcquisitionSecondsAfterBoot();

	td->timerhealth->has_buildepoch = true;
	td->timerhealth->buildepoch = BUILD_EPOCH;
	return td;
}