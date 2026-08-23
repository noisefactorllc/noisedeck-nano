#include "i2c_bus.h"
#include <string.h>

static const int TIMEOUT_MS = 100;

bool I2cBus::begin(gpio_num_t scl, gpio_num_t sda, i2c_port_t port) {
  i2c_master_bus_config_t cfg = {};
  cfg.clk_source = I2C_CLK_SRC_DEFAULT;
  cfg.i2c_port = port;
  cfg.scl_io_num = scl;
  cfg.sda_io_num = sda;
  cfg.glitch_ignore_cnt = 7;
  cfg.flags.enable_internal_pullup = true;
  return i2c_new_master_bus(&cfg, &bus_) == ESP_OK;
}

i2c_master_dev_handle_t I2cBus::add(uint8_t addr, uint32_t hz) {
  i2c_device_config_t cfg = {};
  cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  cfg.device_address = addr;
  cfg.scl_speed_hz = hz;
  i2c_master_dev_handle_t dev = nullptr;
  if (i2c_master_bus_add_device(bus_, &cfg, &dev) != ESP_OK) return nullptr;
  return dev;
}

bool I2cBus::probe(uint8_t addr) { return i2c_master_probe(bus_, addr, TIMEOUT_MS) == ESP_OK; }

bool I2cBus::write_reg(i2c_master_dev_handle_t dev, uint8_t reg, const uint8_t *data, size_t len) {
  uint8_t buf[33];
  if (!dev || len > sizeof(buf) - 1) return false;
  buf[0] = reg;
  memcpy(buf + 1, data, len);
  return i2c_master_transmit(dev, buf, len + 1, TIMEOUT_MS) == ESP_OK;
}

bool I2cBus::read_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *data, size_t len) {
  if (!dev) return false;
  return i2c_master_transmit_receive(dev, &reg, 1, data, len, TIMEOUT_MS) == ESP_OK;
}

bool I2cBus::write_read(i2c_master_dev_handle_t dev, const uint8_t *w, size_t wlen, uint8_t *r, size_t rlen) {
  if (!dev) return false;
  return i2c_master_transmit_receive(dev, w, wlen, r, rlen, TIMEOUT_MS) == ESP_OK;
}
