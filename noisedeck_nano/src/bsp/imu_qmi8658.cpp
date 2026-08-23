#include "imu_qmi8658.h"

enum { REG_WHO_AM_I = 0x00, REG_CTRL1 = 0x02, REG_CTRL2 = 0x03, REG_CTRL7 = 0x08, REG_AX_L = 0x35 };

bool ImuQmi8658::begin(I2cBus &bus, uint8_t addr) {
  bus_ = &bus;
  dev_ = bus.add(addr, 400000);
  uint8_t who = 0;
  if (!dev_ || !bus.read_reg(dev_, REG_WHO_AM_I, &who, 1) || who != 0x05) return false;
  const uint8_t ctrl1 = 0x60;   // address auto-increment, little-endian sample registers
  const uint8_t ctrl2 = 0x23;   // +-8 g range, 1 kHz output data rate
  const uint8_t ctrl7 = 0x01;   // accelerometer enabled, gyroscope off
  return bus.write_reg(dev_, REG_CTRL1, &ctrl1, 1) && bus.write_reg(dev_, REG_CTRL2, &ctrl2, 1) &&
         bus.write_reg(dev_, REG_CTRL7, &ctrl7, 1);
}

bool ImuQmi8658::read_accel(int16_t *ax, int16_t *ay, int16_t *az) {
  uint8_t d[6];
  if (!bus_->read_reg(dev_, REG_AX_L, d, 6)) return false;
  *ax = (int16_t)(d[0] | (d[1] << 8));
  *ay = (int16_t)(d[2] | (d[3] << 8));
  *az = (int16_t)(d[4] | (d[5] << 8));
  return true;
}
