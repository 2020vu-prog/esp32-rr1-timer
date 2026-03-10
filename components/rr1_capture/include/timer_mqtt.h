#pragma once

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
void mq_pub(char *msg);
void fmtJson(char *buf, size_t bufs, jsonTagP tagsHead);
double epoch_double();