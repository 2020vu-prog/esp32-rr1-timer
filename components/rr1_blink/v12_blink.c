#include "timer_blink.h"

#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_log.h"
#include "rr1_pin_defs.h"

#define V12_BLINK_GPIO RR1_PIN_12V_ENABLE
#define V12_BLINK_TIMER_HZ 1000000
#define V12_BLINK_ALARM_TICKS (V12_BLINK_TIMER_HZ / 1)

static const char *TAG = "v12_blink";
static gptimer_handle_t v12_blink_timer;
static volatile bool v12_blink_level;
static volatile uint32_t v12_blink_ticks;

static bool IRAM_ATTR
v12_blink_timer_cb(gptimer_handle_t timer,
                   const gptimer_alarm_event_data_t *edata, void *user_ctx) {
  (void)timer;
  (void)edata;
  (void)user_ctx;

  //  v12_blink_level = !v12_blink_level;
  v12_blink_ticks++;
  if (v12_blink_ticks % 10 > 0) {
    v12_blink_level = true; // force on every 10th tick for testing
  } else {
    v12_blink_level = false;
  }

  gpio_set_level(V12_BLINK_GPIO, v12_blink_level);
  return false;
}

uint32_t get_v12_blink_ticks(void) { return v12_blink_ticks; }

void init_v12_blink(void) {
  if (v12_blink_timer) {
    return;
  }

  gptimer_config_t timer_config = {
      .clk_src = GPTIMER_CLK_SRC_DEFAULT,
      .direction = GPTIMER_COUNT_UP,
      .resolution_hz = V12_BLINK_TIMER_HZ,
  };
  ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &v12_blink_timer));

  gptimer_event_callbacks_t cbs = {
      .on_alarm = v12_blink_timer_cb,
  };
  ESP_ERROR_CHECK(
      gptimer_register_event_callbacks(v12_blink_timer, &cbs, NULL));

  gptimer_alarm_config_t alarm_config = {
      .alarm_count = V12_BLINK_ALARM_TICKS,
      .reload_count = 0,
      .flags.auto_reload_on_alarm = true,
  };
  ESP_ERROR_CHECK(gptimer_set_alarm_action(v12_blink_timer, &alarm_config));
  ESP_ERROR_CHECK(gptimer_enable(v12_blink_timer));
  ESP_ERROR_CHECK(gptimer_start(v12_blink_timer));

  ESP_LOGI(TAG, "Started 10Hz v12 blink timer on GPIO %d", V12_BLINK_GPIO);
}
