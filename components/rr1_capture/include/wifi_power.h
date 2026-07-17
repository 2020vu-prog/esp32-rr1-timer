#pragma once

typedef enum {
  WIFI_POWER_HOLD_MQTT_PUBLISH = 1U << 0,
  WIFI_POWER_HOLD_OTA = 1U << 1,
  WIFI_POWER_HOLD_MQTT_CONNECT = 1U << 2,
} wifi_power_hold_t;

void wifiPowerHold(wifi_power_hold_t hold, const char *reason);
void wifiPowerRelease(wifi_power_hold_t hold, const char *reason);
int getRecentWifiPsMinModemPercentAverage(void);
