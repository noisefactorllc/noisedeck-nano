// QMI8658 6-axis IMU over I2C, accelerometer only, used for shake detection.
#pragma once

#include "i2c_bus.h"

class ImuQmi8658 {
public:
  // Verifies WHO_AM_I, configures +-8 g at 1 kHz, enables the accelerometer.
  bool begin(I2cBus &bus, uint8_t addr);
  // Raw accelerometer sample, 4096 LSB per g at +-8 g.
  bool read_accel(int16_t *ax, int16_t *ay, int16_t *az);

private:
  I2cBus *bus_ = nullptr;
  i2c_master_dev_handle_t dev_ = nullptr;
};
