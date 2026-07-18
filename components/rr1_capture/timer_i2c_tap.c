#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdio.h>
#include <timer_blink.h>

#include "rr1_pin_defs.h"
#include "timer_i2c_tap.h"

#define I2C_MASTER_SDA_IO RR1_PIN_I2C_DATA
#define I2C_MASTER_SCL_IO RR1_PIN_I2C_CLK

#define LIS3DH_I2C_ADDRESS 0x18 // Usually 0x19 or 0x18 depending on SDO/SA0 pin
#define LIS3DH_INT1_PIN RR1_PIN_MOTION_INT
#define LIS3DH_CLK_SPEED_HZ 100000
#define LIS3DH_I2C_TIMEOUT_MS 100

// LIS3DH Registers
#define LIS3DH_REG_CTRL1 0x20
#define LIS3DH_REG_CTRL2 0x21
#define LIS3DH_REG_CTRL3 0x22
#define LIS3DH_REG_CTRL4 0x23
#define LIS3DH_REG_CTRL5 0x24
#define LIS3DH_REG_CLICK_CFG 0x38
#define LIS3DH_REG_CLICK_SRC 0x39
#define LIS3DH_REG_CLICK_THS 0x3A
#define LIS3DH_REG_TIME_LIMIT 0x3B
#define LIS3DH_REG_TIME_LATENCY 0x3C
#define LIS3DH_REG_TIME_WINDOW 0x3D

static const char *TAG = "LIS3DH_TAP";
static i2c_master_dev_handle_t i2c_dev_handle;
static i2c_master_bus_handle_t i2c_bus_handle;
static QueueHandle_t gpio_evt_queue = NULL;
static uint8_t lis3dh_tap_threshold = 0x20;

#define RECENT_CLICKS_SIZE 10
static uint8_t recentClickIndex = 0; // Index for circular buffer
static uint64_t recentClicks[RECENT_CLICKS_SIZE] = {
    0}; // Array to store recent click timestamps
// I2C Write Helper
static esp_err_t lis3dh_write_reg(uint8_t reg, uint8_t data) {
  uint8_t write_buffer[2] = {reg, data};
  return i2c_master_transmit(i2c_dev_handle, write_buffer, sizeof(write_buffer),
                             LIS3DH_I2C_TIMEOUT_MS);
}

// I2C Read Helper
static esp_err_t lis3dh_read_reg(uint8_t reg, uint8_t *data) {
  return i2c_master_transmit_receive(i2c_dev_handle, &reg, 1, data, 1,
                                     LIS3DH_I2C_TIMEOUT_MS);
}

static esp_err_t lis3dh_init_write(uint8_t reg, uint8_t data,
                                   const char *name) {
  esp_err_t err = lis3dh_write_reg(reg, data);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "LIS3DH init failed writing %s: %s", name,
             esp_err_to_name(err));
  }
  return err;
}

static void lis3dh_cleanup_i2c(void) {
  if (i2c_dev_handle != NULL) {
    esp_err_t err = i2c_master_bus_rm_device(i2c_dev_handle);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "Failed to remove LIS3DH I2C device: %s",
               esp_err_to_name(err));
    }
    i2c_dev_handle = NULL;
  }

  if (i2c_bus_handle != NULL) {
    esp_err_t err = i2c_del_master_bus(i2c_bus_handle);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "Failed to delete LIS3DH I2C bus: %s",
               esp_err_to_name(err));
    }
    i2c_bus_handle = NULL;
  }
}

void audit_tap_history() {
  uint64_t now = esp_timer_get_time();
  int tapCount = 0;

  // Count taps in the last 5 seconds (5,000,000 microseconds)
  for (int i = 0; i < RECENT_CLICKS_SIZE; i++) {
    if (recentClicks[i] > now - 5000000) {
      tapCount++;
    }
  }

  ESP_LOGI(TAG, "Taps in last 5 seconds: %d", tapCount);
  if (tapCount >= 5) {
    memcpy(recentClicks, (uint64_t[RECENT_CLICKS_SIZE]){0},
           sizeof(recentClicks));
    toggle_visible_laser();
  }
}
// GPIO Interrupt receive Task
static void gpio_lis3dh_tap_task(void *arg) {
  uint32_t io_num;
  uint8_t click_src;

  for (;;) {
    if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
      // INT1 triggered, read CLICK_SRC to clear interrupt and find tap
      // direction
      esp_err_t err = lis3dh_read_reg(LIS3DH_REG_CLICK_SRC, &click_src);
      if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read CLICK_SRC: %s", esp_err_to_name(err));
        continue;
      }

      // Mask to check single/double tap
      if (click_src & 0x20) { // DCLICK bit is set (0x20)
        ESP_LOGI(TAG, "Double Tap Detected! CLICK_SRC: 0x%02X", click_src);
      } else if (click_src & 0x10) { // SCLICK bit is set (0x10)
        ESP_LOGI(TAG, "Single Tap Detected! CLICK_SRC: 0x%02X", click_src);
        recentClicks[recentClickIndex] =
            esp_timer_get_time(); // Store timestamp of this tap
        recentClickIndex =
            (recentClickIndex + 1) %
            RECENT_CLICKS_SIZE; // Move to next index in circular buffer

        audit_tap_history(); // Call function to analyze tap history for
                             // patterns
      }
    }
  }
}

// ISR Handler for INT1
static void IRAM_ATTR gpio_isr_handler(void *arg) {
  uint32_t gpio_num = (uint32_t)arg;
  xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

void rr1_i2c_tap_reinit(uint8_t tap_threshold) {
  lis3dh_tap_threshold = tap_threshold;
  if (i2c_dev_handle == NULL) {
    ESP_LOGI(TAG, "LIS3DH not initialized; retrying init with threshold 0x%02X",
             lis3dh_tap_threshold);
    rr1_i2c_tap_init();
    return;
  }

  esp_err_t err = lis3dh_init_write(LIS3DH_REG_CLICK_THS, lis3dh_tap_threshold,
                                    "CLICK_THS");
  if (err == ESP_OK) {
    ESP_LOGI(TAG, "LIS3DH tap threshold set to 0x%02X", lis3dh_tap_threshold);
  }
}

void rr1_i2c_tap_init(void) {
  esp_err_t err;

  ESP_LOGI(TAG, "Initializing I2C and LIS3DH...");

  // 1. Install I2C Master Bus
  i2c_master_bus_config_t i2c_bus_config = {
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .i2c_port = I2C_NUM_0,
      .scl_io_num = I2C_MASTER_SCL_IO,
      .sda_io_num = I2C_MASTER_SDA_IO,
      .glitch_ignore_cnt = 7,
      .flags.enable_internal_pullup = true,
  };
  err = i2c_new_master_bus(&i2c_bus_config, &i2c_bus_handle);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "LIS3DH tap detection disabled: I2C bus init failed: %s",
             esp_err_to_name(err));
    return;
  }

  // 2. Add LIS3DH Device to Bus
  i2c_device_config_t dev_cfg = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = LIS3DH_I2C_ADDRESS,
      .scl_speed_hz = LIS3DH_CLK_SPEED_HZ,
  };
  err = i2c_master_bus_add_device(i2c_bus_handle, &dev_cfg, &i2c_dev_handle);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "LIS3DH tap detection disabled: I2C device add failed: %s",
             esp_err_to_name(err));
    lis3dh_cleanup_i2c();
    return;
  }

  // 3. Configure LIS3DH
  // CTRL1: Normal power mode, 100Hz ODR, Enable XYZ axes
  err = lis3dh_init_write(LIS3DH_REG_CTRL1, 0x57, "CTRL1");
  if (err != ESP_OK) {
    goto init_failed;
  }
  // CTRL3: Click interrupt on INT1 pin
  err = lis3dh_init_write(LIS3DH_REG_CTRL3, 0x80, "CTRL3");
  if (err != ESP_OK) {
    goto init_failed;
  }
  // CLICK_CFG: Enable double tap detection on all axes (or 0x15 for single
  // tap)
  err = lis3dh_init_write(LIS3DH_REG_CLICK_CFG, 0x15, "CLICK_CFG");
  if (err != ESP_OK) {
    goto init_failed;
  }
  // CLICK_THS: Set tap threshold
  err = lis3dh_init_write(LIS3DH_REG_CLICK_THS, lis3dh_tap_threshold,
                          "CLICK_THS");
  if (err != ESP_OK) {
    goto init_failed;
  }
  // CLICK Time Limits (Adjust based on timing needs)
  err = lis3dh_init_write(LIS3DH_REG_TIME_LIMIT, 0x10, "TIME_LIMIT");
  if (err != ESP_OK) {
    goto init_failed;
  }
  err = lis3dh_init_write(LIS3DH_REG_TIME_LATENCY, 0x18, "TIME_LATENCY");
  if (err != ESP_OK) {
    goto init_failed;
  }
  err = lis3dh_init_write(LIS3DH_REG_TIME_WINDOW, 0xC0, "TIME_WINDOW");
  if (err != ESP_OK) {
    goto init_failed;
  }

  // 4. Configure ESP32 GPIO Interrupt for INT1
  gpio_config_t io_conf = {
      .intr_type = GPIO_INTR_POSEDGE, // Trigger on rising edge
      .pin_bit_mask = (1ULL << LIS3DH_INT1_PIN),
      .mode = GPIO_MODE_INPUT,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .pull_up_en = GPIO_PULLUP_DISABLE,
  };
  err = gpio_config(&io_conf);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "LIS3DH tap detection disabled: GPIO config failed: %s",
             esp_err_to_name(err));
    lis3dh_cleanup_i2c();
    return;
  }

  // Create a queue to handle gpio event from ISR
  gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
  if (gpio_evt_queue == NULL) {
    ESP_LOGW(TAG, "LIS3DH tap detection disabled: GPIO queue create failed");
    lis3dh_cleanup_i2c();
    return;
  }

  // Install gpio isr service and hook isr handler
  err = gpio_install_isr_service(0);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGW(TAG, "LIS3DH tap detection disabled: ISR service init failed: %s",
             esp_err_to_name(err));
    vQueueDelete(gpio_evt_queue);
    gpio_evt_queue = NULL;
    lis3dh_cleanup_i2c();
    return;
  }

  err = gpio_isr_handler_add(LIS3DH_INT1_PIN, gpio_isr_handler,
                             (void *)LIS3DH_INT1_PIN);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "LIS3DH tap detection disabled: ISR handler add failed: %s",
             esp_err_to_name(err));
    vQueueDelete(gpio_evt_queue);
    gpio_evt_queue = NULL;
    lis3dh_cleanup_i2c();
    return;
  }

  BaseType_t task_created =
      xTaskCreate(gpio_lis3dh_tap_task, "LIS3DH_TAP", 2048, NULL, 10, NULL);
  if (task_created != pdPASS) {
    ESP_LOGW(TAG, "LIS3DH tap detection disabled: task create failed");
    gpio_isr_handler_remove(LIS3DH_INT1_PIN);
    vQueueDelete(gpio_evt_queue);
    gpio_evt_queue = NULL;
    lis3dh_cleanup_i2c();
    return;
  }

  ESP_LOGI(TAG, "Setup complete. Waiting for taps...");
  return;

init_failed:
  ESP_LOGW(TAG, "LIS3DH tap detection disabled; continuing without tap input");
  lis3dh_cleanup_i2c();
}
