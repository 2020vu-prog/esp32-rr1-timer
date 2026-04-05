
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum blink_output_t
{
	BLINK_OUTPUT_LASER,
	BLINK_OUTPUT_LED,
} blink_output_t;

typedef enum blink_pattern_t
{
	BLINK_PATTERN_WIFI_PROVISIONING_ERROR,
	BLINK_PATTERN_WIFI_CONNECTION_ERROR,
	BLINK_PATTERN_OK,
	BLINK_PATTERN_MQTT_ERROR,

} blink_pattern_t;

typedef enum error_pri_t
{
	ERROR_PRI_NONE,
	ERROR_PRI_WIFI_PROVISIONING,
	ERROR_PRI_WIFI_CONNECTION,
	ERROR_PRI_MQTT,
	ERROR_PRI_MAX,
} error_pri_t;

typedef void (*applyCallbackFunc)(enum blink_output_t output);
void registerApplyCallback(enum blink_output_t output, applyCallbackFunc f);
int64_t do_blink(enum blink_output_t output, uint64_t nowMs);
void init_blink();
void apply_blink_pattern(enum blink_output_t output, enum blink_pattern_t pattern);
void set_error_priority(enum error_pri_t pri,bool isActive);
