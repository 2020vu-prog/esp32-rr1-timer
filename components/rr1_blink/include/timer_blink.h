
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum blink_output_t {
    BLINK_OUTPUT_LASER,
    BLINK_OUTPUT_LED,
} blink_output_t;

typedef enum blink_pattern_t {
    BLINK_PATTERN_WIFI_ERROR	,
    BLINK_PATTERN_OK,
    BLINK_PATTERN_MQTT_ERROR,
    
} blink_pattern_t;   

typedef void (*applyCallbackFunc)(enum blink_output_t output);
void registerApplyCallback(enum blink_output_t output, applyCallbackFunc f);
int64_t do_blink(enum blink_output_t output, uint64_t nowMs);
void init_blink_gpio();
void apply_blink_pattern(enum blink_output_t output, enum blink_pattern_t pattern);

