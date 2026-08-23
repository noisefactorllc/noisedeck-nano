// Thin wrapper over the ESP-IDF i2c_master driver shared by the PMIC, touch and IMU.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <driver/i2c_master.h>

class I2cBus {
public:
  bool begin(gpio_num_t scl, gpio_num_t sda, i2c_port_t port);
  i2c_master_bus_handle_t handle() const { return bus_; }
  i2c_master_dev_handle_t add(uint8_t addr, uint32_t hz);
  bool probe(uint8_t addr);
  bool write_reg(i2c_master_dev_handle_t dev, uint8_t reg, const uint8_t *data, size_t len);
  bool read_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *data, size_t len);
  bool write_read(i2c_master_dev_handle_t dev, const uint8_t *w, size_t wlen, uint8_t *r, size_t rlen);

private:
  i2c_master_bus_handle_t bus_ = nullptr;
};
