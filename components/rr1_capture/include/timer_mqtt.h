#pragma once


struct _rr1MqHandle ;
typedef struct _rr1MqHandle *rr1MqHandle;
typedef struct _jsonTag
{
    char *tag;
    char *valStr;
    int64_t val64;
    struct _jsonTag *next;
} jsonTag;
typedef jsonTag *jsonTagP;

void mqtt_app_start(void);
int mq_pub64(char *msg);
int getMqttConnectionCount();
int getMqttRecentLatencyMs();
void mq_pub(char *msg);
void fmtJson(char *buf, size_t bufs, jsonTagP tagsHead);
double epoch_double();