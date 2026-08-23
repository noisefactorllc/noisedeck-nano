#include "touch_cst9220.h"
#include <driver/gpio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

bool TouchCst9220::begin(I2cBus &bus, uint8_t addr, gpio_num_t rst, int width, int height) {
  bus_ = &bus;
  w_ = width;
  h_ = height;
  gpio_config_t io = {};
  io.mode = GPIO_MODE_OUTPUT;
  io.pin_bit_mask = 1ULL << rst;
  io.pull_up_en = GPIO_PULLUP_ENABLE;
  gpio_config(&io);
  gpio_set_level(rst, 1);
  vTaskDelay(pdMS_TO_TICKS(20));
  gpio_set_level(rst, 0);
  vTaskDelay(pdMS_TO_TICKS(20));
  gpio_set_level(rst, 1);
  vTaskDelay(pdMS_TO_TICKS(150));
  dev_ = bus.add(addr, 400000);
  return dev_ != nullptr && bus.probe(addr);
}

bool TouchCst9220::read(uint16_t *x, uint16_t *y) {
  // Register 0xD000: [0] event/status, [1..3] packed 12-bit point, [5] point count, [6] 0xAB marker.
  const uint8_t reg[2] = {0xD0, 0x00};
  uint8_t d[10] = {0};
  if (!bus_->write_read(dev_, reg, 2, d, 10)) return false;
  if (d[6] != 0xAB || (d[5] & 0x7F) == 0 || (d[0] & 0x0F) != 0x06) return false;
  uint16_t ry = (uint16_t)((d[1] << 4) | (d[3] >> 4));
  uint16_t rx = (uint16_t)((d[2] << 4) | (d[3] & 0x0F));
  if (ry > h_ - 1) ry = (uint16_t)(h_ - 1);
  if (rx > w_ - 1) rx = (uint16_t)(w_ - 1);
  *y = ry;
  *x = (uint16_t)((w_ - 1) - rx);   // the panel's x axis runs opposite to the touch sensor's
  return true;
}
