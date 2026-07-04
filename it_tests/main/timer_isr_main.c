#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "rr1_it_timer_isr";
static const uint64_t TIMER_RESOLUTION_HZ = 1000000;
static const uint64_t TIMER_PERIOD_US = 10000;
static const uint32_t TICK_MS = 10;
static const uint32_t FINISH_PERIOD_SHORT_TICKS = 40000 / TICK_MS;
static const uint32_t FINISH_PERIOD_LONG_TICKS = 80000 / TICK_MS;
static const uint32_t BLOCKED_TICKS = 150 / TICK_MS;
static const uint32_t MAX_FINISH_OFFSET_TICKS = 4000 / TICK_MS;

static const gpio_num_t LANE_1_GPIO = GPIO_NUM_0;
static const gpio_num_t LANE_2_GPIO = GPIO_NUM_1;
static const gpio_num_t USER_LED_GPIO = GPIO_NUM_27;

typedef struct {
  gpio_num_t gpio;
  const char *name;
  uint32_t trigger_tick;
  uint32_t release_tick;
  bool blocked;
  bool complete;
} lane_sim_t;

static volatile uint32_t s_isr_ticks;
static TaskHandle_t s_tick_task;

static void configure_outputs(void) {
  const gpio_config_t output_config = {
      .pin_bit_mask = (1ULL << LANE_1_GPIO) | (1ULL << LANE_2_GPIO) |
                      (1ULL << USER_LED_GPIO),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };

  ESP_ERROR_CHECK(gpio_config(&output_config));
  ESP_ERROR_CHECK(gpio_set_level(LANE_1_GPIO, 1));
  ESP_ERROR_CHECK(gpio_set_level(LANE_2_GPIO, 1));
  ESP_ERROR_CHECK(gpio_set_level(USER_LED_GPIO, 0));
}

static uint32_t random_finish_offset_ticks(void) {
  return esp_random() % (MAX_FINISH_OFFSET_TICKS + 1);
}

static uint32_t next_finish_interval_ticks(bool *use_long_interval) {
  uint32_t interval_ticks =
      *use_long_interval ? FINISH_PERIOD_LONG_TICKS : FINISH_PERIOD_SHORT_TICKS;
  *use_long_interval = !*use_long_interval;
  return interval_ticks;
}

static void start_finish(lane_sim_t lanes[2], uint32_t now_ticks) {
  uint32_t lane_1_offset = random_finish_offset_ticks();
  uint32_t lane_2_offset = random_finish_offset_ticks();

  lanes[0] = (lane_sim_t){
      .gpio = LANE_1_GPIO,
      .name = "lane 1",
      .trigger_tick = now_ticks + lane_1_offset,
      .release_tick = now_ticks + lane_1_offset + BLOCKED_TICKS,
  };
  lanes[1] = (lane_sim_t){
      .gpio = LANE_2_GPIO,
      .name = "lane 2",
      .trigger_tick = now_ticks + lane_2_offset,
      .release_tick = now_ticks + lane_2_offset + BLOCKED_TICKS,
  };

  ESP_ERROR_CHECK(gpio_set_level(LANE_1_GPIO, 1));
  ESP_ERROR_CHECK(gpio_set_level(LANE_2_GPIO, 1));
  ESP_ERROR_CHECK(gpio_set_level(USER_LED_GPIO, 1));

  const char *winner = "tie";
  if (lane_1_offset < lane_2_offset) {
    winner = "lane 1";
  } else if (lane_2_offset < lane_1_offset) {
    winner = "lane 2";
  }

  ESP_LOGI(TAG,
           "finish started: lane 1=%" PRIu32 " ms, lane 2=%" PRIu32
           " ms, winner=%s",
           lane_1_offset * TICK_MS, lane_2_offset * TICK_MS, winner);
}

static void update_lane(lane_sim_t *lane, uint32_t now_ticks) {
  if (lane->complete) {
    return;
  }

  if (!lane->blocked && now_ticks >= lane->trigger_tick) {
    ESP_ERROR_CHECK(gpio_set_level(lane->gpio, 0));
    lane->blocked = true;
    ESP_LOGI(TAG, "%s blocked", lane->name);
  }

  if (lane->blocked && now_ticks >= lane->release_tick) {
    ESP_ERROR_CHECK(gpio_set_level(lane->gpio, 1));
    lane->complete = true;
    ESP_LOGI(TAG, "%s clear", lane->name);
  }
}

static bool IRAM_ATTR on_timer_alarm(gptimer_handle_t timer,
                                     const gptimer_alarm_event_data_t *edata,
                                     void *user_ctx) {
  (void)timer;
  (void)edata;
  (void)user_ctx;

  BaseType_t high_task_woken = pdFALSE;
  ++s_isr_ticks;

  if (s_tick_task != NULL) {
    vTaskNotifyGiveFromISR(s_tick_task, &high_task_woken);
  }

  return high_task_woken == pdTRUE;
}

void app_main(void) {
  s_tick_task = xTaskGetCurrentTaskHandle();
  configure_outputs();

  const gptimer_config_t timer_config = {
      .clk_src = GPTIMER_CLK_SRC_DEFAULT,
      .direction = GPTIMER_COUNT_UP,
      .resolution_hz = TIMER_RESOLUTION_HZ,
  };

  gptimer_handle_t timer = NULL;
  ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &timer));

  const gptimer_event_callbacks_t callbacks = {
      .on_alarm = on_timer_alarm,
  };
  ESP_ERROR_CHECK(gptimer_register_event_callbacks(timer, &callbacks, NULL));

  const gptimer_alarm_config_t alarm_config = {
      .alarm_count = TIMER_PERIOD_US,
      .reload_count = 0,
      .flags.auto_reload_on_alarm = true,
  };
  ESP_ERROR_CHECK(gptimer_set_alarm_action(timer, &alarm_config));
  ESP_ERROR_CHECK(gptimer_enable(timer));
  ESP_ERROR_CHECK(gptimer_start(timer));

  ESP_LOGI(TAG, "10 ms GPTimer interrupt started");

  lane_sim_t lanes[2] = {0};
  bool finish_active = false;
  bool use_long_finish_interval = false;
  uint32_t next_finish_tick =
      next_finish_interval_ticks(&use_long_finish_interval);
  uint32_t processed_ticks = 0;

  while (true) {
    uint32_t pending_ticks = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1500));
    if (pending_ticks == 0) {
      ESP_LOGW(TAG, "No timer ISR notification received");
      continue;
    }

    uint32_t target_ticks = s_isr_ticks;
    while (processed_ticks != target_ticks) {
      uint32_t now_ticks = ++processed_ticks;

      if (!finish_active && now_ticks >= next_finish_tick) {
        start_finish(lanes, now_ticks);
        finish_active = true;
        next_finish_tick +=
            next_finish_interval_ticks(&use_long_finish_interval);
      }

      if (finish_active) {
        update_lane(&lanes[0], now_ticks);
        update_lane(&lanes[1], now_ticks);

        if (lanes[0].complete && lanes[1].complete) {
          ESP_ERROR_CHECK(gpio_set_level(USER_LED_GPIO, 0));
          finish_active = false;
          ESP_LOGI(TAG, "finish complete at tick %" PRIu32, now_ticks);
        }
      }

      if ((now_ticks % 100) == 0) {
        ESP_LOGI(TAG, "ISR ticks: %" PRIu32, now_ticks);
      }
    }
  }
}
