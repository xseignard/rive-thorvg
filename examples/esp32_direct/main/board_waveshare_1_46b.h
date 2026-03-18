#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

bool waveshare_1_46b_display_init(void);
void waveshare_1_46b_display_flush_rgb565(const uint16_t* pixels, int width, int height);
esp_err_t waveshare_1_46b_display_flush_rgb565_async(const uint16_t* pixels, int width, int height);
esp_err_t waveshare_1_46b_display_wait_for_flush(uint32_t timeout_ms);

bool waveshare_1_46b_touch_init(void);
bool waveshare_1_46b_touch_read(uint16_t* x, uint16_t* y, bool* pressed);
