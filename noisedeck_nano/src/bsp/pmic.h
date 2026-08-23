// AXP2101 power management: brings up the board's 3.3 V rails and owns the
// panel's supply (ALDO3), which doubles as the display reset.
#pragma once

#include "i2c_bus.h"

bool pmic_begin(I2cBus &bus, uint8_t addr);
void pmic_panel_power(bool on);
