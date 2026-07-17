#include "wifi_power.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

static const char *TAG = "wifi_power";

#define RECENT_WIFI_PS_MAX 9
#define WIFI_PS_INVALID -999

static int recentWifiPsMinModemPercents[RECENT_WIFI_PS_MAX] = {
    WIFI_PS_INVALID, WIFI_PS_INVALID, WIFI_PS_INVALID,
    WIFI_PS_INVALID, WIFI_PS_INVALID, WIFI_PS_INVALID,
    WIFI_PS_INVALID, WIFI_PS_INVALID, WIFI_PS_INVALID};
static int recentWifiPsIndex = 0;
static wifi_ps_type_t currentWifiPsMode = WIFI_PS_NONE;
static int64_t wifiPsLastChangeUs = 0;
static int64_t wifiPsMinModemTotalUs = 0;
static int64_t wifiPsLastSampleUs = 0;
static int64_t wifiPsLastSampleMinModemUs = 0;
static atomic_bool mqttPublishHold = false;
static atomic_bool otaHold = false;
static atomic_bool mqttConnectHold = false;

static int64_t getWifiPsMinModemTotalUs(int64_t nowUs) {
  int64_t totalUs = wifiPsMinModemTotalUs;
  if (wifiPsLastChangeUs > 0 && currentWifiPsMode == WIFI_PS_MIN_MODEM) {
    totalUs += nowUs - wifiPsLastChangeUs;
  }
  return totalUs;
}

static void noteWifiPowerSaveMode(wifi_ps_type_t mode) {
  int64_t nowUs = esp_timer_get_time();
  if (wifiPsLastChangeUs == 0) {
    wifiPsLastChangeUs = nowUs;
    wifiPsLastSampleUs = nowUs;
    wifiPsLastSampleMinModemUs = wifiPsMinModemTotalUs;
  } else if (currentWifiPsMode == WIFI_PS_MIN_MODEM) {
    wifiPsMinModemTotalUs += nowUs - wifiPsLastChangeUs;
    wifiPsLastChangeUs = nowUs;
  } else {
    wifiPsLastChangeUs = nowUs;
  }
  currentWifiPsMode = mode;
}

static void setTrackedWifiPowerSaveMode(wifi_ps_type_t mode,
                                        const char *reason) {
  esp_err_t err = esp_wifi_set_ps(mode);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "esp_wifi_set_ps(%d) failed for %s: %s", mode, reason,
             esp_err_to_name(err));
    return;
  }
  noteWifiPowerSaveMode(mode);
}

static void applyTrackedWifiPowerSaveMode(const char *reason) {
  bool holdPsNone = atomic_load(&mqttPublishHold) || atomic_load(&otaHold) ||
                    atomic_load(&mqttConnectHold);
  wifi_ps_type_t mode = holdPsNone ? WIFI_PS_NONE : WIFI_PS_MIN_MODEM;
  setTrackedWifiPowerSaveMode(mode, reason);
}

static atomic_bool *holdState(wifi_power_hold_t hold) {
  switch (hold) {
  case WIFI_POWER_HOLD_MQTT_PUBLISH:
    return &mqttPublishHold;
  case WIFI_POWER_HOLD_OTA:
    return &otaHold;
  case WIFI_POWER_HOLD_MQTT_CONNECT:
    return &mqttConnectHold;
  default:
    return NULL;
  }
}

static void setWifiPowerHold(wifi_power_hold_t hold, bool enabled,
                             const char *reason) {
  atomic_bool *state = holdState(hold);
  if (!state) {
    ESP_LOGW(TAG, "setWifiPowerHold: invalid hold %d for %s", hold, reason);
    return;
  }

  bool prior = atomic_exchange(state, enabled);
  if (prior != enabled) {
    applyTrackedWifiPowerSaveMode(reason);
  }
}

void wifiPowerHold(wifi_power_hold_t hold, const char *reason) {
  setWifiPowerHold(hold, true, reason);
}

void wifiPowerRelease(wifi_power_hold_t hold, const char *reason) {
  setWifiPowerHold(hold, false, reason);
}

int getRecentWifiPsMinModemPercentAverage(void) {
  int64_t nowUs = esp_timer_get_time();
  if (wifiPsLastSampleUs == 0) {
    wifiPsLastSampleUs = nowUs;
    wifiPsLastSampleMinModemUs = getWifiPsMinModemTotalUs(nowUs);
    return 0;
  }

  int64_t totalMinModemUs = getWifiPsMinModemTotalUs(nowUs);
  int64_t elapsedUs = nowUs - wifiPsLastSampleUs;
  if (elapsedUs > 0) {
    int64_t minModemUs = totalMinModemUs - wifiPsLastSampleMinModemUs;
    int percent = (int)((minModemUs * 100) / elapsedUs);
    if (percent < 0) {
      percent = 0;
    } else if (percent > 100) {
      percent = 100;
    }
    recentWifiPsMinModemPercents[recentWifiPsIndex] = percent;
    recentWifiPsIndex = (recentWifiPsIndex + 1) % RECENT_WIFI_PS_MAX;
    wifiPsLastSampleUs = nowUs;
    wifiPsLastSampleMinModemUs = totalMinModemUs;
  }

  int sum = 0;
  int count = 0;
  for (int i = 0; i < RECENT_WIFI_PS_MAX; i++) {
    if (recentWifiPsMinModemPercents[i] != WIFI_PS_INVALID) {
      sum += recentWifiPsMinModemPercents[i];
      count++;
    }
  }
  return count > 0 ? sum / count : 0;
}
