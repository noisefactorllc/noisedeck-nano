#include "display.h"
#include <Arduino.h>
#include <string.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "bsp/board.h"
#include "bsp/esp_lcd_sh8601.h"
#include "bsp/pmic.h"
#include "fx/fx.h"

static esp_lcd_panel_handle_t panel = NULL;
static esp_lcd_panel_io_handle_t io = NULL;
static SemaphoreHandle_t free_slots = NULL;
static uint16_t *bufs[2] = {NULL, NULL};

// Vendor init sequence for the CO5300 on this board (from the Waveshare BSP).
static const sh8601_lcd_init_cmd_t INIT_CMDS[] = {
  {0x11, (uint8_t[]){0x00}, 0, 600},
  {0xFE, (uint8_t[]){0x20}, 1, 0},
  {0x19, (uint8_t[]){0x10}, 1, 0},
  {0x1C, (uint8_t[]){0xA0}, 1, 0},
  {0xFE, (uint8_t[]){0x00}, 1, 0},
  {0xC4, (uint8_t[]){0x80}, 1, 0},
  {0x3A, (uint8_t[]){0x55}, 1, 0},
  {0x35, (uint8_t[]){0x00}, 1, 0},
  {0x36, (uint8_t[]){0x30}, 1, 0},
  {0x53, (uint8_t[]){0x20}, 1, 0},
  {0x51, (uint8_t[]){0xFF}, 1, 0},
  {0x63, (uint8_t[]){0xFF}, 1, 0},
  {0x2A, (uint8_t[]){0x00, 0x00, 0x01, 0xDF}, 4, 0},
  {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xDF}, 4, 0},
  {0x29, (uint8_t[]){0x00}, 0, 100},
};

static bool IRAM_ATTR on_color_done(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *) {
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(free_slots, &woken);
  return woken == pdTRUE;
}

#define CHECK(expr, what)                                                          \
  do {                                                                             \
    esp_err_t _e = (expr);                                                         \
    if (_e != ESP_OK) {                                                            \
      Serial.printf("[display] %s failed: %s\n", what, esp_err_to_name(_e));       \
      return false;                                                                \
    }                                                                              \
  } while (0)

bool display_init() {
  free_slots = xSemaphoreCreateCounting(2, 2);

  spi_bus_config_t buscfg = {};
  buscfg.sclk_io_num = BOARD_LCD_PCLK;
  buscfg.data0_io_num = BOARD_LCD_D0;
  buscfg.data1_io_num = BOARD_LCD_D1;
  buscfg.data2_io_num = BOARD_LCD_D2;
  buscfg.data3_io_num = BOARD_LCD_D3;
  buscfg.max_transfer_sz = FX_W * FX_BAND_H * 2;
  CHECK(spi_bus_initialize((spi_host_device_t)BOARD_LCD_SPI, &buscfg, SPI_DMA_CH_AUTO), "spi_bus_initialize");

  esp_lcd_panel_io_spi_config_t io_config = {};
  io_config.cs_gpio_num = BOARD_LCD_CS;
  io_config.dc_gpio_num = -1;
  io_config.spi_mode = 0;
  io_config.pclk_hz = 40 * 1000 * 1000;
  io_config.trans_queue_depth = 2;
  io_config.on_color_trans_done = on_color_done;
  io_config.user_ctx = NULL;
  io_config.lcd_cmd_bits = 32;
  io_config.lcd_param_bits = 8;
  io_config.flags.quad_mode = true;
  CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BOARD_LCD_SPI, &io_config, &io), "new_panel_io_spi");

  sh8601_vendor_config_t vendor_config = {};
  vendor_config.init_cmds = INIT_CMDS;
  vendor_config.init_cmds_size = sizeof(INIT_CMDS) / sizeof(INIT_CMDS[0]);
  vendor_config.flags.use_qspi_interface = 1;

  esp_lcd_panel_dev_config_t panel_config = {};
  panel_config.reset_gpio_num = GPIO_NUM_NC;
  panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
  panel_config.bits_per_pixel = 16;
  panel_config.vendor_config = &vendor_config;
  CHECK(esp_lcd_new_panel_sh8601(io, &panel_config, &panel), "new_panel_sh8601");

  // The panel has no reset line; its supply rail (PMIC ALDO3) is power-cycled instead.
  pmic_panel_power(true);
  vTaskDelay(pdMS_TO_TICKS(100));
  pmic_panel_power(false);
  vTaskDelay(pdMS_TO_TICKS(100));
  pmic_panel_power(true);
  vTaskDelay(pdMS_TO_TICKS(100));
  CHECK(esp_lcd_panel_init(panel), "panel_init");

  for (int i = 0; i < 2; i++) {
    bufs[i] = (uint16_t *)heap_caps_malloc(FX_W * FX_BAND_H * 2, MALLOC_CAP_DMA);
    if (!bufs[i]) {
      Serial.println("[display] band buffer alloc failed");
      return false;
    }
  }
  memset(bufs[0], 0, FX_W * FX_BAND_H * 2);
  for (int b = 0; b < FX_BANDS; b++) {
    display_acquire(0, 500);
    display_push(b * FX_BAND_H, FX_BAND_H, bufs[0]);
  }
  display_wait_all(500);
  return true;
}

void display_set_brightness(uint8_t level) {
  if (!io) return;
  uint32_t cmd = (0x02u << 24) | (0x51u << 8);
  esp_lcd_panel_io_tx_param(io, cmd, &level, 1);
}

uint16_t *display_band_buffer(int slot) { return bufs[slot & 1]; }

bool display_acquire(int slot, uint32_t timeout_ms) {
  (void)slot;
  return xSemaphoreTake(free_slots, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

bool display_push(int y0, int rows, uint16_t *buf) {
  esp_err_t e = esp_lcd_panel_draw_bitmap(panel, 0, y0, FX_W, y0 + rows, buf);
  if (e != ESP_OK) {
    xSemaphoreGive(free_slots);
    return false;
  }
  return true;
}

void display_wait_all(uint32_t timeout_ms) {
  // Drain both slots, then hand them back.
  int got = 0;
  for (int i = 0; i < 2; i++)
    if (xSemaphoreTake(free_slots, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) got++;
  for (int i = 0; i < got; i++) xSemaphoreGive(free_slots);
}
