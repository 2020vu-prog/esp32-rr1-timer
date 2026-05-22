#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdio.h>

#include "rr1_pin_defs.h"

#define I2C_MASTER_SDA_IO RR1_PIN_I2C_DATA
#define I2C_MASTER_SCL_IO RR1_PIN_I2C_CLK

#define LIS3DH_I2C_ADDRESS 0x18 // Usually 0x19 or 0x18 depending on SDO/SA0 pin
#define LIS3DH_INT1_PIN RR1_PIN_MOTION_INT
#define LIS3DH_CLK_SPEED_HZ 100000

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
static QueueHandle_t gpio_evt_queue = NULL;

// I2C Write Helper
static esp_err_t lis3dh_write_reg(uint8_t reg, uint8_t data) {
  uint8_t write_buffer[2] = {reg, data};
  return i2c_master_transmit(i2c_dev_handle, write_buffer, sizeof(write_buffer),
                             -1);
}

// I2C Read Helper
static esp_err_t lis3dh_read_reg(uint8_t reg, uint8_t *data) {
  return i2c_master_transmit_receive(i2c_dev_handle, &reg, 1, data, 1, -1);
}

// GPIO Interrupt Task
static void gpio_lis3dh_tap_task(void *arg) {
  uint32_t io_num;
  uint8_t click_src;

  for (;;) {
    if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
      // INT1 triggered, read CLICK_SRC to clear interrupt and find tap
      // direction
      lis3dh_read_reg(LIS3DH_REG_CLICK_SRC, &click_src);

      // Mask to check single/double tap
      if (click_src & 0x20) { // DCLICK bit is set (0x20)
        ESP_LOGI(TAG, "Double Tap Detected! CLICK_SRC: 0x%02X", click_src);
      } else if (click_src & 0x10) { // SCLICK bit is set (0x10)
        ESP_LOGI(TAG, "Single Tap Detected! CLICK_SRC: 0x%02X", click_src);
      }
    }
  }
}

// ISR Handler for INT1
static void IRAM_ATTR gpio_isr_handler(void *arg) {
  uint32_t gpio_num = (uint32_t)arg;
  xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}
void rr1_i2c_tap_init(void) {

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
  i2c_master_bus_handle_t bus_handle;
  ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &bus_handle));

  // 2. Add LIS3DH Device to Bus
  i2c_device_config_t dev_cfg = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = LIS3DH_I2C_ADDRESS,
      .scl_speed_hz = LIS3DH_CLK_SPEED_HZ,
  };
  ESP_ERROR_CHECK(
      i2c_master_bus_add_device(bus_handle, &dev_cfg, &i2c_dev_handle));

  // 3. Configure LIS3DH
  // CTRL1: Normal power mode, 100Hz ODR, Enable XYZ axes
  ESP_ERROR_CHECK(lis3dh_write_reg(LIS3DH_REG_CTRL1, 0x57));
  // CTRL3: Click interrupt on INT1 pin
  ESP_ERROR_CHECK(lis3dh_write_reg(LIS3DH_REG_CTRL3, 0x80));
  // CLICK_CFG: Enable double tap detection on all axes (or 0x15 for single tap)
  ESP_ERROR_CHECK(lis3dh_write_reg(LIS3DH_REG_CLICK_CFG, 0x2A));
  // CLICK_THS: Set tap threshold
  ESP_ERROR_CHECK(lis3dh_write_reg(LIS3DH_REG_CLICK_THS, 0x15));
  // CLICK Time Limits (Adjust based on timing needs)
  ESP_ERROR_CHECK(lis3dh_write_reg(LIS3DH_REG_TIME_LIMIT, 0x10)); // Time limit
  ESP_ERROR_CHECK(
      lis3dh_write_reg(LIS3DH_REG_TIME_LATENCY, 0x18)); // Time latency
  ESP_ERROR_CHECK(
      lis3dh_write_reg(LIS3DH_REG_TIME_WINDOW, 0xC0)); // Time window

  // 4. Configure ESP32 GPIO Interrupt for INT1
  gpio_config_t io_conf = {
      .intr_type = GPIO_INTR_POSEDGE, // Trigger on rising edge
      .pin_bit_mask = (1ULL << LIS3DH_INT1_PIN),
      .mode = GPIO_MODE_INPUT,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .pull_up_en = GPIO_PULLUP_DISABLE,
  };
  ESP_ERROR_CHECK(gpio_config(&io_conf));

  // Create a queue to handle gpio event from ISR
  gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
  xTaskCreate(gpio_lis3dh_tap_task, "LIS3DH_TAP", 2048, NULL, 10, NULL);

  // Install gpio isr service and hook isr handler
  ESP_ERROR_CHECK(gpio_install_isr_service(0));
  ESP_ERROR_CHECK(gpio_isr_handler_add(LIS3DH_INT1_PIN, gpio_isr_handler,
                                       (void *)LIS3DH_INT1_PIN));

  ESP_LOGI(TAG, "Setup complete. Waiting for taps...");
}
