// Pin map for the Waveshare ESP32-C6-Touch-AMOLED-2.16 (per the vendor schematic).
#pragma once

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <driver/spi_master.h>

#define BOARD_I2C_PORT I2C_NUM_0
#define BOARD_I2C_SCL GPIO_NUM_7
#define BOARD_I2C_SDA GPIO_NUM_8

#define BOARD_LCD_SPI SPI2_HOST
#define BOARD_LCD_CS GPIO_NUM_15
#define BOARD_LCD_PCLK GPIO_NUM_0
#define BOARD_LCD_D0 GPIO_NUM_1
#define BOARD_LCD_D1 GPIO_NUM_2
#define BOARD_LCD_D2 GPIO_NUM_3
#define BOARD_LCD_D3 GPIO_NUM_4
#define BOARD_LCD_W 480
#define BOARD_LCD_H 480

#define BOARD_TOUCH_RST GPIO_NUM_11
#define BOARD_TOUCH_INT GPIO_NUM_5
#define BOARD_TOUCH_ADDR 0x5A

#define BOARD_PMIC_ADDR 0x34
#define BOARD_IMU_ADDR 0x6B
#define BOARD_BUTTON GPIO_NUM_9
