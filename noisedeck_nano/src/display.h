// CO5300 AMOLED over QSPI via esp_lcd, pushed as horizontal bands from two DMA
// buffers so the CPU renders band N+1 while band N is in flight.
#pragma once

#include <stdint.h>

bool display_init();
void display_set_brightness(uint8_t level);

uint16_t *display_band_buffer(int slot);
bool display_acquire(int slot, uint32_t timeout_ms);
bool display_push(int y0, int rows, uint16_t *buf);
void display_wait_all(uint32_t timeout_ms);
