#include "driver/gpio.h"
#include "driver/mcpwm_cap.h"
#include "esp_log.h"
#include "esp_private/esp_clk.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "cpu_idle.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "gps_xlate.h"
#include "quad_uint32.h"
#include "timer_blink.h"
#include "timer_capture.h"
#include "timer_hist.h"
#include "timer_mqtt.h"

#include <math.h>
#include <time.h>
// #include <uuid.h>

const static char *TAG = "rr1_capture";

qcontrol_handle *quad_h;
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Please update the following configuration according to your
/// board spec ////////////////////////////
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
      pull_up : true,
      neg_edge : true,
      pos_edge : true,
      pinHandlerFunc : pinHandlerLane,
    },
    {
      gpio : LANE2_GPIO,
      lane_index : 1,
      pname : "lane2",
      pull_up : true,
      neg_edge : true,
      pos_edge : true,
      pinHandlerFunc : pinHandlerLane,
    },
};
pindef_t *gpsPindef = &pindefs[0];
pindef_t *l1Pindef = &pindefs[1];
pindef_t *l2Pindef = &pindefs[2];

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

IRAM_ATTR static bool
mcpwm_timer_isr_callback(mcpwm_cap_channel_handle_t cap_chan,
                         const mcpwm_capture_event_data_t *edata,
                         void *user_data) {
  /*
  static uint32_t cap_val_begin_of_sample = 0;
  static uint32_t cap_val_end_of_sample = 0;
  TaskHandle_t task_to_notify = (TaskHandle_t)user_data;
  BaseType_t high_task_wakeup = pdFALSE;

  // calculate the interval in the ISR,
  // so that the interval will be always correct even when capture_queue is not
  handled in time and overflow. if (edata->cap_edge == MCPWM_CAP_EDGE_POS)
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
      xTaskNotifyFromISR(task_to_notify, tof_ticks, eSetValueWithOverwrite,
  &high_task_wakeup);
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
  // uint64_t cap_value64 = ((uint64_t)(edata->cap_value)) | (wrap_count32 <<
  // 32);

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
  if (rc != pdPASS) {
    recv_que_overflow++;
  }
  // ESP_LOGI(TAG, "ISR: %d\n", (int)edata->recv_bytes);

  return need_yield == pdTRUE;
}

mcpwm_cap_channel_handle_t
capture_channel_setup(pindef_t *pd, mcpwm_cap_timer_handle_t cap_timer) {
  ESP_LOGI(TAG, "Install capture channel %s [%d]", pd->pname, pd->gpio);
  mcpwm_cap_channel_handle_t cap_local_chan_h = NULL;
  mcpwm_capture_channel_config_t cap_ch_conf = {
      .gpio_num = pd->gpio,
      .prescale = 1,
      //.prescale = 80,

      // capture on both edge
      .flags.neg_edge = pd->neg_edge,
      .flags.pos_edge = pd->pos_edge,
      // pull up internally
      .flags.pull_down = pd->pull_down,
      .flags.pull_up = pd->pull_up,
  };
  ESP_ERROR_CHECK(
      mcpwm_new_capture_channel(cap_timer, &cap_ch_conf, &cap_local_chan_h));

  ESP_LOGI(TAG, "Register capture callback");
  //  TaskHandle_t cur_task = xTaskGetCurrentTaskHandle();
  mcpwm_capture_event_callbacks_t cbs = {
      .on_cap = mcpwm_timer_isr_callback,
  };

  ESP_ERROR_CHECK(mcpwm_capture_channel_register_event_callbacks(
      cap_local_chan_h, &cbs, pd));

  ESP_LOGI(TAG, "Enable capture channel");
  ESP_ERROR_CHECK(mcpwm_capture_channel_enable(cap_local_chan_h));
  pd->channel_h = cap_local_chan_h;
  return cap_local_chan_h;
}
static mcpwm_cap_timer_handle_t gcap_timer = NULL;
#define TPS61040_ENABLE_GPIO 10
esp_err_t TPS61040_init() {
  // Configure the GPIO pin for TPS61040 enable
  gpio_config_t io_conf = {};
  io_conf.intr_type = GPIO_INTR_DISABLE; // No interrupt
  io_conf.mode = GPIO_MODE_OUTPUT_OD;    // adafruit eval board pulls high to
                                         // enable, so use open drain output
  io_conf.pin_bit_mask = (1ULL << TPS61040_ENABLE_GPIO); // Pin mask
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;          // No pull-down
  io_conf.pull_up_en = GPIO_PULLUP_DISABLE;              // No pull-up
  esp_err_t ret = gpio_config(&io_conf);
  ESP_RETURN_ON_ERROR(ret, TAG, "Failed to configure GPIO for TPS61040 enable");

  // Enable the TPS61040 by setting the GPIO high
  ret = gpio_set_level(TPS61040_ENABLE_GPIO, 1);
  ESP_RETURN_ON_ERROR(ret, TAG, "Failed to set GPIO level for TPS61040 enable");

  return ESP_OK;
}
esp_err_t capture_setup(void) {
  timer_hist_init();
  esp_err_t ret = ESP_OK;

  // Create the receive queue
  recv_que =
      xQueueCreateWithCaps(ESP_PROBE_DEFAULT_Q_DEPTH,
                           sizeof(esp_probe_recv_data_t), ESP_PROBE_ALLOC_CAPS);
  ESP_GOTO_ON_FALSE(recv_que, ESP_ERR_NO_MEM, err, TAG,
                    "no memory for receive queue");

  mcpwm_cap_timer_handle_t cap_timer = NULL;
  mcpwm_capture_timer_config_t cap_conf = {
      .clk_src = MCPWM_CAPTURE_CLK_SRC_DEFAULT,
      .resolution_hz = 1 * 1000 * 1000, // 1MHz, prescaler implicitly handled

      .group_id = 0,
  };
  ESP_ERROR_CHECK(mcpwm_new_capture_timer(&cap_conf, &cap_timer));

  // Calculate the total size of the array in bytes
  size_t array_size_bytes = sizeof(pindefs);
  // Calculate the size of a single element in bytes
  size_t element_size_bytes = sizeof(pindefs[0]);
  // Calculate the number of elements
  size_t array_length = array_size_bytes / element_size_bytes;
  for (int x = 0; x < array_length; x++) {
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
static uint64_t softGpsUs = 0;
static uint64_t realGpsUs = 0;
static uint64_t staleGpsUs = 0;
struct PollFunc {
  void (*func)(struct PollFunc *);
  uint64_t freqMs;
  uint64_t nextMs;
  uint64_t nowMs;
};
typedef struct PollFunc PollFunc;

void quadWatchdog(PollFunc *pf) {

  if (pf->nowMs < (realGpsUs / 1000) + 2000) {
    // pps active
  } else {
    // no pps for 2 seconds, force a soft one to keep the quad alive
    ESP_LOGI(TAG, "quadWatchdog: forcing soft gps");
    softGpsUs = esp_timer_get_time();
    mcpwm_capture_channel_trigger_soft_catch(gpsPindef->channel_h);
  }
}
void awakenPoll() {
  esp_probe_recv_data_t recv_data = {
      .cap_value64 = 0,
      .cap_edge = 0,
      .pin_user_data = NULL,

  };

  xQueueSend(recv_que, &recv_data, 0); // Can block
}
void blinkUserLed(PollFunc *pf) {
  pf->nextMs = do_blink(BLINK_OUTPUT_LED, pf->nowMs);
}
void blinkLaser(PollFunc *pf) {
  pf->nextMs = do_blink(BLINK_OUTPUT_LASER, pf->nowMs);
}

void simulateLaneActivity(PollFunc *pf) {
  static bool pup = false;
  // mcpwm_capture_channel_trigger_soft_catch(l1Pindef->channel_h); // TODO
  // delete after test
  mcpwm_capture_channel_trigger_soft_catch(
      l2Pindef->channel_h); // TODO delete after test
  if (pup) {
    gpio_set_pull_mode(LANE1_GPIO, GPIO_PULLUP_ONLY);
  } else {
    gpio_set_pull_mode(LANE1_GPIO, GPIO_PULLDOWN_ONLY);
  }
  pup = !pup;
}

void mqPollDataList() { mqPubDataList(); }

void mqIncCreditsPeriodically(PollFunc *pf) {
  // mq_pub("health30");
  incMqttPublishCredits();
  mqPubDataList();
}
void idleCalc(PollFunc *pf) {
  statsRecap_t recap = {};
  updateCpuIdleStats(&recap);
  ESP_LOGI(TAG, "idleCalc: cpu  percent %d idle percent %d",
           (int)recap.cpu_used_percent, (int)recap.cpu_idle_percent);
}
PollFunc pollFuncs[] = {
    {.func = mqPollDataList, .freqMs = 999999000, .nextMs = 0}, // event driven
    {.func = mqIncCreditsPeriodically, .freqMs = 15000, .nextMs = 0},
    //{.func = simulateLaneActivity, .freqMs = 10000, .nextMs = 0},
    {.func = quadWatchdog, .freqMs = 45000, .nextMs = 0},
    {.func = blinkUserLed, .freqMs = 1000, .nextMs = 0},
    {.func = blinkLaser, .freqMs = 1000, .nextMs = 0},
    {.func = idleCalc, .freqMs = 10000, .nextMs = 0},
    {.func = NULL, .freqMs = 0, .nextMs = 0} // sentinel

};
void reset_blink_poll(blink_output_t output) {
  for (int x = 0; pollFuncs[x].func != NULL; x++) {
    if ((output == BLINK_OUTPUT_LED && pollFuncs[x].func == blinkUserLed) ||
        (output == BLINK_OUTPUT_LASER && pollFuncs[x].func == blinkLaser)) {
      pollFuncs[x].nextMs = 0; // reset to run immediately
    }
  }
  awakenPoll();
}
void scheduleMqPubDataList(int delayMs) {
  uint64_t nowMs = esp_timer_get_time() / 1000;
  for (int x = 0; pollFuncs[x].func != NULL; x++) {
    if (pollFuncs[x].func == mqPollDataList) {
      pollFuncs[x].nextMs = nowMs + delayMs;
    }
  }
  awakenPoll(); // recalc next poll
}

void capture_main_xtask(void *pvParameters) { capture_main(); }
int doPollAll() {

  const uint64_t nowMs = esp_timer_get_time() / 1000;
  int delayMs = 10000;
  for (int x = 0; pollFuncs[x].func != NULL; x++) {
    if (nowMs >= pollFuncs[x].nextMs) {
      const uint64_t innerNowMs = esp_timer_get_time() / 1000;
      pollFuncs[x].nowMs = nowMs;
      pollFuncs[x].nextMs = nowMs + pollFuncs[x].freqMs;
      pollFuncs[x].func(&pollFuncs[x]);

      const uint64_t innerElapsedMs =
          (esp_timer_get_time() / 1000) - innerNowMs;
      if (innerElapsedMs > 2) {
        ESP_LOGW(TAG, "INNER poll func is SLOW! %d ms [%d]",
                 (int)innerElapsedMs, x);
      }
    }
    // min delay until next poll func needs to run
    if (delayMs > pollFuncs[x].nextMs - nowMs) {
      delayMs = pollFuncs[x].nextMs - nowMs;
    }
  }
  // mqIncCreditsPeriodically();
  const uint64_t elapsedMs = (esp_timer_get_time() / 1000) - nowMs;
  if (elapsedMs > 2) {
    ESP_LOGW(TAG, "doPollAll: polling is SLOW! %d ms", (int)elapsedMs);
  }
  return delayMs;
}
void capture_main(void) {
  init_blink();
  registerApplyCallback(BLINK_OUTPUT_LED, reset_blink_poll);
  registerApplyCallback(BLINK_OUTPUT_LASER, reset_blink_poll);

  quad_h = init_qcontrol();
  TPS61040_init();
  capture_setup();
  ESP_LOGI(TAG, "Install capture timer");

#ifdef CONFIG_UUID_CUSTOM_GENERATION
  uuid_set_mode(UUID_MODE_RANDOM);
  uuid_set_mode(UUID_MODE_VARIANT4);

  uuid_init();

  const char *uuid_ran = uuid_generate();
#endif

  uint64_t priorv = 0;
  while (1) {

    int delayMs = doPollAll();
    ESP_LOGD(TAG, "xQueueReceive: top");
#ifdef CONFIG_UUID_CUSTOM_GENERATION
    ESP_LOGI(TAG, "Generated UUID: %s", uuid_ran);
#endif
    // wait for echo done signal
    /// uint32_t timer_value = mcpwm_capture_timer_get_value(gcap_timer);
    // uint32_t timer_value = mcpwm_capture_signal_get_value(gcap_timer);
    uint32_t timer_value = 17;
    char buf[128] = {};
    snprintf(buf, 128, "%s %ld", TAG, timer_value);

    // mq_pub(buf);

    esp_probe_recv_data_t recv_data = {};
    if (delayMs < 10) {
      delayMs = 10;
    }
    ESP_LOGD(TAG, "xQueueReceive: waiting for %d ms", delayMs);
    if (xQueueReceive(recv_que, &recv_data, pdMS_TO_TICKS(delayMs)) == pdTRUE) {
      if (recv_data.cap_value64 == 0 && recv_data.cap_edge == 0 &&
          recv_data.pin_user_data == NULL) {
        ESP_LOGI(TAG, "xQueueReceive: woke for poll");
        continue; // woke for poll, not isr
      }
      apply64bitHysterisis(&recv_data);
      uint64_t elapsed = recv_data.cap_value64 - priorv;
      // pindef_t *pd = (pindef_t *)recv_data.pin_user_data;
      pindef_t *pd = recv_data.pin_user_data;
      ESP_LOGI(TAG, "xQueueReceive got: %" PRIu64 " %d %d %s",
               recv_data.cap_value64, (int)pd->gpio, (int)recv_data.cap_edge,
               pd->pname);
      snprintf(buf, 128, "gpio: %" PRIu64 " %s %d E:%" PRIu64,
               recv_data.cap_value64, pd->pname, (int)recv_data.cap_edge,
               elapsed);
      if (recv_data.cap_edge) // wip
      {

        priorv = recv_data.cap_value64;
        mq_pub(buf);
      }
      if (pd && pd->pinHandlerFunc) {
        uint64_t nowUs = esp_timer_get_time();
        pd->pinHandlerFunc(&recv_data);
        uint64_t handlerElapsedUs = esp_timer_get_time() - nowUs;
        if (handlerElapsedUs > 2000) {
          ESP_LOGW(TAG, "pinHandlerFunc is SLOW! %d ms",
                   (int)handlerElapsedUs / 1000);
        }
      }
    } else {
    }
  }
}

void apply64bitHysterisis(esp_probe_recv_data_t *recv_dataP) {

  recv_dataP->cap_value64 &= 0x00000000FFFFFFFF;
  recv_dataP->cap_value64 = qc_get64(quad_h, recv_dataP->cap_value64);
}
/*
 calculate high order bits, based on CBU up time.
  assumes this will be called shortly after the time stamp is captured
*/
void apply64bitHysterisisOLD(esp_probe_recv_data_t *recv_dataP) {
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
  // uint64_t cap_value64 = ((uint64_t)(edata->cap_value)) | (wrap_count32 <<
  // 32);
}

void pinHandlerLane(esp_probe_recv_data_t *recv_dataP) {
  th_append(recv_dataP);
  scheduleMqPubDataList(100);
}
// real gps will suppress q timeout and therefore soft gps
bool isSoftGps(uint64_t nowGpsUs) {
  if (nowGpsUs - softGpsUs > 10000000) {
    return false;
  }
  return true;
}
int gpsInitialAcquisitionSecondsAfterBoot = 0;
int gpsUptimeTotalSeconds = 0;
int gpsUptimeContiguousSeconds = 0;
int gpsFlutter = 0;
bool gpsemittingpps = false;
void pinHandlerGps(esp_probe_recv_data_t *recv_dataP) {
  uint64_t nowGpsUs = esp_timer_get_time();

  if (isSoftGps(nowGpsUs)) {
    ESP_LOGI(TAG, "pinHandlerGps : skipping soft");
    gpsemittingpps = false;
    gpsUptimeContiguousSeconds = 0;
    if (staleGpsUs != realGpsUs) {
      staleGpsUs = realGpsUs;
      gpsFlutter++;
    }

    return;
  }

  // this is a real gps event, reset the real gps timer and update health stats
  if (!gpsInitialAcquisitionSecondsAfterBoot) {
    gpsInitialAcquisitionSecondsAfterBoot = nowGpsUs / 1000000;
  }
  gpsemittingpps = true;
  realGpsUs = nowGpsUs;
  gpsUptimeTotalSeconds++;
  gpsUptimeContiguousSeconds++;
  log_gps_pps(recv_dataP);
}
int getGpsInitialAcquisitionSecondsAfterBoot() {
  return gpsInitialAcquisitionSecondsAfterBoot;
}
int getGpsUptimeTotalSeconds() { return gpsUptimeTotalSeconds; }
int getGpsFlutter() { return gpsFlutter; }
bool isGpsEmittingPps() { return gpsemittingpps; }
int getgpsUptimeContiguousSeconds() { return gpsUptimeContiguousSeconds; }
