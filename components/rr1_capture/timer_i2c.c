#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

#define TAG "LIS3DH"

// I2C config
#define I2C_PORT 0
#define I2C_SDA_PIN 23
#define I2C_SCL_PIN 24
#define I2C_FREQ_HZ 400000

#define LIS3DH_ADDR 0x18 // or 0x19 depending on SA0

// GPIO interrupt pin
#define LIS3DH_INT1_GPIO 1

static i2c_master_bus_handle_t bus_handle;
static i2c_master_dev_handle_t dev_handle;

// --- Low-level I2C write ---
esp_err_t lis3dh_write(uint8_t reg, uint8_t data) {
  uint8_t buf[2] = {reg, data};
  return i2c_master_transmit(dev_handle, buf, sizeof(buf), -1);
}

// --- Low-level I2C read ---
esp_err_t lis3dh_read(uint8_t reg, uint8_t *data) {
  return i2c_master_transmit_receive(dev_handle, &reg, 1, data, 1, -1);
}

// --- Initialize I2C ---
void i2c_init(void) {
  i2c_master_bus_config_t bus_config = {
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .i2c_port = I2C_PORT,
      .sda_io_num = I2C_SDA_PIN,
      .scl_io_num = I2C_SCL_PIN,
      .glitch_ignore_cnt = 7,
      .flags.enable_internal_pullup = true,
  };
  ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus_handle));

  i2c_device_config_t dev_config = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = LIS3DH_ADDR,
      .scl_speed_hz = I2C_FREQ_HZ,
  };
  ESP_ERROR_CHECK(
      i2c_master_bus_add_device(bus_handle, &dev_config, &dev_handle));
}

// --- Configure LIS3DH for motion interrupt ---
void lis3dh_init(void) {
  // CTRL_REG1: 100 Hz, all axes enabled
  lis3dh_write(0x20, 0x57);

  // CTRL_REG4: ±2g, high-resolution
  lis3dh_write(0x23, 0x08);

  // CTRL_REG3: enable INT1 on AOI1
  lis3dh_write(0x22, 0x40);

  // INT1_CFG: enable X/Y/Z high event
  lis3dh_write(0x30, 0x2A);
  // 0x2A = XH | YH | ZH

  // INT1_THS: threshold (~16 mg/LSB @2g)
  lis3dh_write(0x32, 0x10); // adjust sensitivity

  // INT1_DURATION: duration (number of samples)
  lis3dh_write(0x33, 0x01);

  ESP_LOGI(TAG, "LIS3DH configured for motion interrupt");
}

// --- GPIO ISR ---
static void IRAM_ATTR lis3dh_isr_handler(void *arg) {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  // Notify a task instead of doing I2C in ISR
  vTaskNotifyGiveFromISR((TaskHandle_t)arg, &xHigherPriorityTaskWoken);
  if (xHigherPriorityTaskWoken) {
    portYIELD_FROM_ISR();
  }
}

// --- Task to handle interrupt ---
void lis3dh_task(void *arg) {
  uint8_t src;

  while (1) {
    // Wait for interrupt notification
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    // Read INT1_SRC to clear interrupt
    lis3dh_read(0x31, &src);

    ESP_LOGI(TAG, "Motion detected! INT1_SRC=0x%02X", src);
  }
}

// --- Setup GPIO interrupt ---
void gpio_init(TaskHandle_t task_handle) {
  gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << LIS3DH_INT1_GPIO),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .intr_type = GPIO_INTR_POSEDGE,
  };
  gpio_config(&io_conf);

  gpio_install_isr_service(0);
  gpio_isr_handler_add(LIS3DH_INT1_GPIO, lis3dh_isr_handler,
                       (void *)task_handle);
}

// --- Main ---
void rr1_i2c_init(void) {
  i2c_init();
  lis3dh_init();

  TaskHandle_t task_handle;
  xTaskCreate(lis3dh_task, "lis3dh_task", 4096, NULL, 5, &task_handle);

  gpio_init(task_handle);
}