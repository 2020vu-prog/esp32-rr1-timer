#include "timer_blink.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "rr1_pin_defs.h"

#define GPIO_PIN_LASER RR1_PIN_VISIBLE_LASER
#define GPIO_PIN_LED RR1_PIN_SEEED_C5_ONBOARD_LED

const static char *TAG = "rr1_blink";
bool error_recap[ERROR_PRI_MAX];
int error_transition_count[ERROR_PRI_MAX];
typedef struct {
  bool state;
  uint32_t period_ms;
} timer_action_t;

typedef struct {
  timer_action_t *actions;
  uint8_t repeat_count;
} timer_repeat_t;

atomic_uint visibleLaserEnabledSeconds = 0;

static timer_repeat_t longSlowBlink[] = {
    {

        .actions =
            (timer_action_t[]){
                {.state = true, .period_ms = 500},
                {.state = false, .period_ms = 500},
                {},
            },
        .repeat_count = 0x1, // Repeat indefinitely
    },
    {},
};
static timer_action_t fastBlinkTemplate[] = {

    {.state = true, .period_ms = 250},
    {.state = false, .period_ms = 260},
    {},

};
static timer_action_t resetDelayTemplate[] = {

    {.state = false, .period_ms = 1500},
    {},

};

static timer_repeat_t fastBlink2[] = {
    {
        .actions = resetDelayTemplate,
        .repeat_count = 1,
    },

    {
        .actions = fastBlinkTemplate,
        .repeat_count = 2,
    },
    {},
};

static timer_repeat_t fastBlink3[] = {
    {
        .actions = resetDelayTemplate,
        .repeat_count = 1,
    },
    {
        .actions = fastBlinkTemplate,
        .repeat_count = 3,
    },
    {},
};

static timer_repeat_t fastBlink4[] = {
    {
        .actions = resetDelayTemplate,
        .repeat_count = 1,
    },
    {
        .actions = fastBlinkTemplate,
        .repeat_count = 4,
    },
    {},
};

static timer_repeat_t fastBlink5[] = {
    {
        .actions = resetDelayTemplate,
        .repeat_count = 1,
    },
    {
        .actions = fastBlinkTemplate,
        .repeat_count = 5,
    },
    {},
};
typedef struct blink_handler_t {
  applyCallbackFunc applyCallback;
  timer_repeat_t *blink_pattern;
  int current_repeat_index;
  int current_action_index;
  int current_repeat_count;
  bool invert;

} blink_handler_t;
blink_handler_t laserBlinkHandler = {
    .blink_pattern = longSlowBlink,
    .current_repeat_index = 0,
    .current_action_index = 0,
};
blink_handler_t ledBlinkHandler = {
    .blink_pattern = fastBlink2,
    .current_repeat_index = 0,
    .current_action_index = 0,
    .invert = true,
};

blink_handler_t *get_blink_handler(enum blink_output_t output);
timer_action_t *get_current_action(blink_handler_t *handler);
void advance_blink_handler(blink_handler_t *handler);
bool get_error_priority(enum error_pri_t pri) {
  if (pri <= ERROR_PRI_NONE || pri >= ERROR_PRI_MAX)
    return false; // Handle invalid priority

  return error_recap[pri];
}
/*
 */
int get_transition_count(enum error_pri_t pri) {
  if (pri <= ERROR_PRI_NONE || pri >= ERROR_PRI_MAX)
    return -1; // Handle invalid priority

  return error_transition_count[pri];
}
/*
**
*/
void set_error_priority(enum error_pri_t pri, bool isActive) {
  if (pri <= ERROR_PRI_NONE || pri >= ERROR_PRI_MAX)
    return; // Handle invalid priority

  if (error_recap[pri] != isActive) {
    error_transition_count[pri]++;
    ESP_LOGI(TAG, "Error priority %d transition count %d", pri,
             error_transition_count[pri]);
  }
  error_recap[pri] = isActive;
  enum error_pri_t lowest_active_pri = ERROR_PRI_NONE;
  for (int i = ERROR_PRI_NONE; i < ERROR_PRI_MAX; i++) {
    if (error_recap[i]) {
      lowest_active_pri = i;
      break;
    }
  }
  // Apply the corresponding blink pattern based on the highest active error
  // priority
  switch (lowest_active_pri) {
  case ERROR_PRI_WIFI_PROVISIONING:
    apply_blink_pattern(BLINK_OUTPUT_LED,
                        BLINK_PATTERN_WIFI_PROVISIONING_ERROR);
    break;
  case ERROR_PRI_WIFI_CONNECTION:
    apply_blink_pattern(BLINK_OUTPUT_LED, BLINK_PATTERN_WIFI_CONNECTION_ERROR);
    break;
  case ERROR_PRI_CREDENTIALS:
    apply_blink_pattern(BLINK_OUTPUT_LED, BLINK_PATTERN_CREDENTIALS_ERROR);
    break;
  case ERROR_PRI_MQTT:
    apply_blink_pattern(BLINK_OUTPUT_LED, BLINK_PATTERN_MQTT_ERROR);
    break;
  default:
    apply_blink_pattern(BLINK_OUTPUT_LED, BLINK_PATTERN_OK);
    break;
  }
}
timer_repeat_t *get_blink_pattern(enum blink_pattern_t pattern) {
  switch (pattern) {
  case BLINK_PATTERN_WIFI_PROVISIONING_ERROR:
    return fastBlink2;
  case BLINK_PATTERN_WIFI_CONNECTION_ERROR:
    return fastBlink3;
  case BLINK_PATTERN_CREDENTIALS_ERROR:
    return fastBlink4;
  case BLINK_PATTERN_MQTT_ERROR:
    return fastBlink5;

  case BLINK_PATTERN_OK:
    return longSlowBlink;
  default:
    return NULL; // Handle invalid pattern type
  }
};
void apply_blink_pattern(blink_output_t output, enum blink_pattern_t pattern) {
  timer_repeat_t *bp = get_blink_pattern(pattern);
  if (bp == NULL)
    return; // Handle invalid pattern type

  blink_handler_t *tgt_handler = get_blink_handler(output);
  if (tgt_handler == NULL)
    return; // Handle invalid output type

  tgt_handler->blink_pattern = bp;
  tgt_handler->current_repeat_index = 0;
  tgt_handler->current_action_index = 0;
  tgt_handler->current_repeat_count = 0;
  if (tgt_handler->applyCallback) {
    tgt_handler->applyCallback(output);
  }
}

blink_handler_t *get_blink_handler(enum blink_output_t output) {
  switch (output) {
  case BLINK_OUTPUT_LASER:
    return &laserBlinkHandler;
  case BLINK_OUTPUT_LED:
    return &ledBlinkHandler;
  default:
    return NULL; // Handle invalid output type
  }
};
void advance_blink_handler(blink_handler_t *handler) {
  if (handler == NULL || handler->blink_pattern == NULL)
    return;

  timer_repeat_t *current_repeat =
      &handler->blink_pattern[handler->current_repeat_index];
  if (current_repeat == NULL || current_repeat->actions == NULL)
    return;

  handler->current_action_index++;
  if (current_repeat->actions[handler->current_action_index].period_ms == 0) {
    handler->current_action_index = 0;
    handler->current_repeat_count++;
    if (handler->current_repeat_count >= current_repeat->repeat_count) {
      handler->current_repeat_count = 0;
      handler->current_repeat_index++;
    }
    if (handler->blink_pattern[handler->current_repeat_index].actions == NULL) {
      handler->current_repeat_index = 0; // Loop back to the first repeat
    }
  }
}
timer_action_t *get_current_action(blink_handler_t *handler) {
  if (handler == NULL || handler->blink_pattern == NULL)
    return NULL;

  timer_repeat_t *current_repeat =
      &handler->blink_pattern[handler->current_repeat_index];
  if (current_repeat == NULL || current_repeat->actions == NULL)
    return NULL;

  timer_action_t *rc = &current_repeat->actions[handler->current_action_index];
  advance_blink_handler(handler);
  return rc;
}
int get_gpio_pin(enum blink_output_t output) {
  switch (output) {
  case BLINK_OUTPUT_LASER:
    return GPIO_PIN_LASER;
  case BLINK_OUTPUT_LED:
    return GPIO_PIN_LED;
  default:
    return -1; // Handle invalid output type
  }
}
bool laserTimeout() {
  int enabledSeconds = atomic_load(&visibleLaserEnabledSeconds);
  if (enabledSeconds == 0) {
    return true; // Not enabled
  }

  int nowSecs = esp_timer_get_time() / 1000000;
  bool rc = nowSecs - enabledSeconds > 180;
  if (rc) {
    atomic_store(&visibleLaserEnabledSeconds, 0);
    ESP_LOGI(TAG, "Visible laser timeout, disabling laser output");
  }
  return rc;
}
int64_t do_blink(enum blink_output_t output, uint64_t nowMs) {
  blink_handler_t *handler = get_blink_handler(output);
  if (handler == NULL)
    return INT64_MAX;

  timer_action_t *current_action = get_current_action(handler);
  if (current_action == NULL)
    return INT64_MAX; // Handle case where no action is available

  // Here you would implement the actual blinking logic based on
  // current_action->state

  // For example, if current_action->state is true, turn on the LED or laser,
  // otherwise turn it off.

  int tgt_gpio = get_gpio_pin(output);
  if (tgt_gpio < 0)
    return INT64_MAX; // Handle invalid GPIO pin

  int newLevel = current_action->state ? 1 : 0;
  if (output == BLINK_OUTPUT_LASER) {
    if (laserTimeout()) {
      newLevel = 0; // Force laser off if not enabled
    }
  }

  if (handler->invert) {
    newLevel = !newLevel;
  }
  gpio_set_level(tgt_gpio, newLevel);

  return nowMs +
         current_action->period_ms; // Return the period for the next action
}
void init_blink_gpio() {
  gpio_config_t io_conf = {};
  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.mode = GPIO_MODE_OUTPUT;

  // io_conf.pin_bit_mask = (1ULL << GPIO_PIN_LED);
  io_conf.pin_bit_mask = (1ULL << GPIO_PIN_LASER) | (1ULL << GPIO_PIN_LED);
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  gpio_config(&io_conf);
}
void init_blink() {
  init_blink_gpio();
  init_v12_blink();
  error_recap[ERROR_PRI_WIFI_PROVISIONING] = true;
  error_recap[ERROR_PRI_WIFI_CONNECTION] = true;
  error_recap[ERROR_PRI_MQTT] = true;
}

void registerApplyCallback(enum blink_output_t output, applyCallbackFunc f) {

  blink_handler_t *bh = get_blink_handler(output);
  if (bh == NULL)
    return; // Handle invalid output type
  bh->applyCallback = f;
}
void toggle_visible_laser() {
  ESP_LOGI(TAG, "Toggling visible laser output");
  int nowSecs = esp_timer_get_time() / 1000000;

  int enabledSeconds = atomic_load(&visibleLaserEnabledSeconds);
  enabledSeconds = enabledSeconds == 0 ? nowSecs : 0;
  atomic_store(&visibleLaserEnabledSeconds, enabledSeconds);
}
