# Third-party code and attributions

Everything in this repository is MIT-licensed (see [LICENSE](LICENSE)) except as noted here.

## Bundled

- **`noisedeck_nano/src/bsp/esp_lcd_sh8601.c`, `esp_lcd_sh8601.h`**: the SH8601-family
  `esp_lcd` panel driver, copyright 2023 Espressif Systems (Shanghai) CO LTD, licensed under
  the Apache License 2.0 (SPDX headers retained in the files). It drives the CO5300 on this
  board unmodified.
- **`CODE_OF_CONDUCT.md`**: adapted from the Contributor Covenant, version 1.4
  (https://www.contributor-covenant.org), CC BY 4.0.

## Fetched at build time

- **XPowersLib 0.3.3** by Lewis He, MIT, installed by `bin/flash.sh` through the Arduino
  Library Manager. Used for the AXP2101 power management chip.
- **arduino-esp32 3.3.11** (LGPL-2.1 core, Apache-2.0 ESP-IDF components), installed by
  `bin/flash.sh` through the Arduino Boards Manager.

## Hardware facts taken from vendor material

The pin map in `src/bsp/board.h`, the CO5300 initialisation register sequence in
`src/display.cpp`, the AXP2101 rail plan in `src/bsp/pmic.cpp`, and the CST9220 report
format in `src/bsp/touch_cst9220.cpp` were read from Waveshare's schematic and example
projects for the ESP32-C6-Touch-AMOLED-2.16
(https://github.com/waveshareteam/ESP32-C6-Touch-AMOLED-2.16). No Waveshare source code is
included; the drivers here are original implementations of those register-level facts.
