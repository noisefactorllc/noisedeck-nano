#include "pmic.h"
#define XPOWERS_CHIP_AXP2101
#include <XPowersLib.h>

static XPowersPMU pmu;
static I2cBus *bus_ = nullptr;
static i2c_master_dev_handle_t dev_ = nullptr;

static int pmic_read(uint8_t, uint8_t reg, uint8_t *data, uint8_t len) {
  return bus_->read_reg(dev_, reg, data, len) ? 0 : -1;
}

static int pmic_write(uint8_t, uint8_t reg, uint8_t *data, uint8_t len) {
  return bus_->write_reg(dev_, reg, data, len) ? 0 : -1;
}

bool pmic_begin(I2cBus &bus, uint8_t addr) {
  bus_ = &bus;
  dev_ = bus.add(addr, 100000);
  if (!dev_ || !pmu.begin(addr, pmic_read, pmic_write)) return false;

  // Rail plan for this board: DC1 feeds the SoC, ALDO1..4 feed the peripherals,
  // all at 3.3 V; USB input limited to 2 A; gentle single-cell charge profile.
  pmu.setVbusCurrentLimit(XPOWERS_AXP2101_VBUS_CUR_LIM_2000MA);
  if (pmu.getDC1Voltage() != 3300) pmu.setDC1Voltage(3300);
  if (pmu.getALDO1Voltage() != 3300) pmu.setALDO1Voltage(3300);
  if (pmu.getALDO2Voltage() != 3300) pmu.setALDO2Voltage(3300);
  if (pmu.getALDO3Voltage() != 3300) pmu.setALDO3Voltage(3300);
  if (pmu.getALDO4Voltage() != 3300) pmu.setALDO4Voltage(3300);
  pmu.setPrechargeCurr(XPOWERS_AXP2101_PRECHARGE_50MA);
  pmu.setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_500MA);
  pmu.setChargerTerminationCurr(XPOWERS_AXP2101_CHG_ITERM_50MA);
  return true;
}

void pmic_panel_power(bool on) {
  if (on) pmu.enableALDO3();
  else pmu.disableALDO3();
}
