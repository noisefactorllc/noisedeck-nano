// CST9220 capacitive touch controller over I2C: one reset pulse at boot, then a
// 10-byte status/point read per poll.
#pragma once

#include "i2c_bus.h"

class TouchCst9220 {
public:
  bool begin(I2cBus &bus, uint8_t addr, gpio_num_t rst, int width, int height);
  // True while a finger is down; x/y in screen pixels.
  bool read(uint16_t *x, uint16_t *y);

private:
  I2cBus *bus_ = nullptr;
  i2c_master_dev_handle_t dev_ = nullptr;
  int w_ = 0, h_ = 0;
};
