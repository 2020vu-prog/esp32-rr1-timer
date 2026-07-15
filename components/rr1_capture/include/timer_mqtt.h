#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct _rr1MqHandle;
typedef struct _rr1MqHandle *rr1MqHandle;
typedef struct _jsonTag {
  char *tag;
  char *valStr;
  int64_t val64;
  struct _jsonTag *next;
} jsonTag;
typedef jsonTag *jsonTagP;

void mqtt_app_start(void);
void timerMqttWifiDisconnected(const char *reason);
void timerMqttWifiIpReady(void);
int mq_pub64(char *msg, int laneTransitionCount, uint64_t healthMarshalledUs);
int getMqttConnectionCount();
int getMqttRecentLatencyMs();
int getMqttMaxLatencyMs();
int getRecentWifiPsMinModemPercentAverage(void);
bool isMqttPublishPending();
int getMqttInFlightMsgId(void);
int64_t getMqttInFlightAgeMs(void);
bool clearExpiredMqttPublish(void);
bool getMqttInFlightRecap(int *laneTransitionCount,
                          uint64_t *healthMarshalledUs);
void mq_pub(char *msg);
void fmtJson(char *buf, size_t bufs, jsonTagP tagsHead);
double epoch_double();
void get_device_hostname(char *host_name, size_t max);
int getMqttPublishCredits();
void incMqttPublishCredits();
void decrementMqttPublishCredits();
