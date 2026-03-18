#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_task_wdt.h"

#include "rive_thorvg.h"
#include "board_waveshare_1_46b.h"

#define DISP_FB_W 412
#define DISP_FB_H 412
#define RENDER_SCALE 2
#define RIVE_FB_W (DISP_FB_W / RENDER_SCALE)
#define RIVE_FB_H (DISP_FB_H / RENDER_SCALE)
#define TARGET_FPS 30
#define FRAME_LOG_INTERVAL 120
#define RIVE_WORKER_THREADS 4
#define DISPLAY_BUFFER_COUNT 2
#define ENABLE_POINTER_EVENTS 0
#define LOAD_WDT_TIMEOUT_MS 60000
#define NORMAL_WDT_TIMEOUT_MS (CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000)

static const char* TAG = "rive_direct";

extern const uint8_t loader_riv_start[] asm("_binary_loader_riv_start");
extern const uint8_t loader_riv_end[] asm("_binary_loader_riv_end");

typedef struct {
    uint16_t x;
    uint16_t y;
    bool pressed;
} touch_state_t;

static uint32_t* s_renderbuffer;
static uint16_t* s_displaybuffers[DISPLAY_BUFFER_COUNT];

static void configure_task_wdt_timeout(uint32_t timeout_ms, const char* reason) {
    esp_task_wdt_config_t cfg = {
        .timeout_ms = timeout_ms,
        .idle_core_mask = (1U << portNUM_PROCESSORS) - 1U,
        .trigger_panic = true,
    };
    esp_err_t err = esp_task_wdt_reconfigure(&cfg);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "task_wdt timeout=%ums (%s)", (unsigned)timeout_ms, reason);
    } else {
        ESP_LOGW(TAG, "task_wdt reconfigure failed (%s): %s", reason, esp_err_to_name(err));
    }
}

static inline uint16_t swap_u16(uint16_t v) {
    return (uint16_t)((v << 8) | (v >> 8));
}

static inline uint16_t argb8888_to_rgb565_swapped(uint32_t argb) {
    uint16_t rgb565 = (uint16_t)((((argb >> 16) & 0xF8) << 8) |
                                 (((argb >> 8) & 0xFC) << 3) |
                                 ((argb >> 3) & 0x1F));
    return swap_u16(rgb565);
}

static void upscale_argb8888_to_rgb565_swapped(const uint32_t* src,
                                               int src_w,
                                               int src_h,
                                               uint16_t* dst,
                                               int dst_w,
                                               int dst_h) {
    if (!src || !dst || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) return;

    if (dst_w == (src_w * 2) && dst_h == (src_h * 2)) {
        for (int y = 0; y < src_h; ++y) {
            const uint32_t* src_row = src + (y * src_w);
            uint16_t* dst_row0 = dst + ((y * 2) * dst_w);
            uint16_t* dst_row1 = dst_row0 + dst_w;
            for (int x = 0; x < src_w; ++x) {
                uint16_t p = argb8888_to_rgb565_swapped(src_row[x]);
                int dx = x * 2;
                dst_row0[dx] = p;
                dst_row0[dx + 1] = p;
                dst_row1[dx] = p;
                dst_row1[dx + 1] = p;
            }
        }
        return;
    }

    for (int y = 0; y < dst_h; ++y) {
        int sy = (y * src_h) / dst_h;
        if (sy >= src_h) sy = src_h - 1;
        const uint32_t* src_row = src + (sy * src_w);
        uint16_t* dst_row = dst + (y * dst_w);
        for (int x = 0; x < dst_w; ++x) {
            int sx = (x * src_w) / dst_w;
            if (sx >= src_w) sx = src_w - 1;
            dst_row[x] = argb8888_to_rgb565_swapped(src_row[sx]);
        }
    }
}

static bool demo_read_touch(touch_state_t* out_state) {
    if (!out_state) return false;
    return waveshare_1_46b_touch_read(&out_state->x, &out_state->y, &out_state->pressed);
}

static void demo_display_init(void) {
    bool ok = waveshare_1_46b_display_init();
    ESP_LOGI(TAG, "display init: %s", ok ? "ok" : "failed");
}

static void demo_touch_init(void) {
    bool ok = waveshare_1_46b_touch_init();
    ESP_LOGI(TAG, "touch init: %s", ok ? "ok" : "failed");
}

static void demo_display_flush_rgb565(const uint16_t* pixels, int width, int height) {
    waveshare_1_46b_display_flush_rgb565(pixels, width, height);
}

static bool load_demo_riv(const uint8_t** data, size_t* len) {
    if (!data || !len) return false;
    *data = loader_riv_start;
    *len = (size_t)(loader_riv_end - loader_riv_start);
    if (*len == 0) return false;
    return true;
}

void app_main(void) {
    const uint8_t* riv_data = NULL;
    size_t riv_len = 0;
    if (!load_demo_riv(&riv_data, &riv_len)) {
        ESP_LOGE(TAG, "failed to load embedded loader.riv");
        return;
    }
    ESP_LOGI(TAG, "embedded loader.riv bytes: %u", (unsigned)riv_len);

    demo_display_init();
    demo_touch_init();

    rive_thorvg_init(RIVE_WORKER_THREADS);

    size_t render_fb_bytes = (size_t)RIVE_FB_W * (size_t)RIVE_FB_H * sizeof(uint32_t);
    size_t display_fb_bytes = (size_t)DISP_FB_W * (size_t)DISP_FB_H * sizeof(uint16_t);

    // Keep the render target in internal RAM for faster CPU access.
    // The display buffers stay in PSRAM because they are much larger.
    s_renderbuffer = (uint32_t*)heap_caps_malloc(render_fb_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s_renderbuffer) {
        s_renderbuffer = (uint32_t*)heap_caps_malloc(render_fb_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    for (int i = 0; i < DISPLAY_BUFFER_COUNT; ++i) {
        s_displaybuffers[i] = (uint16_t*)heap_caps_malloc(display_fb_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!s_renderbuffer || !s_displaybuffers[0] || !s_displaybuffers[1]) {
        ESP_LOGE(TAG,
                 "framebuffer alloc failed (render=%u display=%u x%d)",
                 (unsigned)render_fb_bytes,
                 (unsigned)display_fb_bytes,
                 DISPLAY_BUFFER_COUNT);
        return;
    }
    memset(s_renderbuffer, 0, render_fb_bytes);
    for (int i = 0; i < DISPLAY_BUFFER_COUNT; ++i) {
        memset(s_displaybuffers[i], 0, display_fb_bytes);
    }

    rive_instance_t* rive = rive_create(s_renderbuffer,
                                        RIVE_FB_W,
                                        RIVE_FB_H,
                                        RIVE_PIXEL_FORMAT_ARGB8888);
    if (!rive) {
        ESP_LOGE(TAG, "rive_create failed");
        rive_thorvg_term();
        return;
    }

    rive_set_fit(rive, RIVE_FIT_CONTAIN, RIVE_ALIGN_CENTER);

    configure_task_wdt_timeout(LOAD_WDT_TIMEOUT_MS, "loading large rive asset");

    bool loaded = rive_load(rive, riv_data, riv_len);

    configure_task_wdt_timeout(NORMAL_WDT_TIMEOUT_MS, "normal runtime");

    if (!loaded) {
        ESP_LOGE(TAG, "rive_load failed for embedded loader.riv");
        rive_destroy(rive);
        rive_thorvg_term();
        return;
    }

    bool was_pressed = false;
    int64_t last_us = esp_timer_get_time();
    int64_t perf_window_start_us = last_us;
    uint32_t changed_count = 0;
    uint64_t advance_total_us = 0;
    uint64_t blit_total_us = 0;
    uint64_t flush_wait_total_us = 0;
    bool flush_pending = false;
    int pending_idx = 0;
    int next_idx = 0;
    uint32_t frame_count = 0;

    for (;;) {
        int64_t now_us = esp_timer_get_time();

        touch_state_t touch;
        if (demo_read_touch(&touch)) {
            float ax = 0.0f;
            float ay = 0.0f;
            float sx = ((float)touch.x * (float)RIVE_FB_W) / (float)DISP_FB_W;
            float sy = ((float)touch.y * (float)RIVE_FB_H) / (float)DISP_FB_H;
            rive_screen_to_artboard(rive, sx, sy, &ax, &ay);

            if (touch.pressed && !was_pressed) {
#if ENABLE_POINTER_EVENTS
                rive_pointer_down(rive, ax, ay);
#endif
            } else if (touch.pressed && was_pressed) {
#if ENABLE_POINTER_EVENTS
                rive_pointer_move(rive, ax, ay);
#endif
            } else if (!touch.pressed && was_pressed) {
#if ENABLE_POINTER_EVENTS
                rive_pointer_up(rive, ax, ay);
#endif
            }
            was_pressed = touch.pressed;
        } else if (was_pressed) {
            was_pressed = false;
        }

        float dt = (float)(now_us - last_us) / 1000000.0f;
        last_us = now_us;

        int64_t advance_start_us = esp_timer_get_time();
        bool changed = rive_advance(rive, dt);
        int64_t advance_end_us = esp_timer_get_time();
        advance_total_us += (uint64_t)(advance_end_us - advance_start_us);
        if (changed) {
            uint16_t* out_display = s_displaybuffers[next_idx];

            int64_t blit_start_us = esp_timer_get_time();
            upscale_argb8888_to_rgb565_swapped(s_renderbuffer,
                                               RIVE_FB_W,
                                               RIVE_FB_H,
                                               out_display,
                                               DISP_FB_W,
                                               DISP_FB_H);
            int64_t blit_end_us = esp_timer_get_time();
            blit_total_us += (uint64_t)(blit_end_us - blit_start_us);

            if (flush_pending) {
                int64_t flush_wait_start_us = esp_timer_get_time();
                if (waveshare_1_46b_display_wait_for_flush(1000) != ESP_OK) {
                    ESP_LOGW(TAG, "display flush wait timeout/error");
                }
                int64_t flush_wait_end_us = esp_timer_get_time();
                flush_wait_total_us += (uint64_t)(flush_wait_end_us - flush_wait_start_us);
                flush_pending = false;
            }

            if (waveshare_1_46b_display_flush_rgb565_async(out_display, DISP_FB_W, DISP_FB_H) == ESP_OK) {
                flush_pending = true;
                pending_idx = next_idx;
                next_idx = pending_idx ^ 1;
            } else {
                demo_display_flush_rgb565(out_display, DISP_FB_W, DISP_FB_H);
            }

            changed_count++;
        }

        frame_count++;
        if ((frame_count % FRAME_LOG_INTERVAL) == 0U) {
            int64_t window_now_us = esp_timer_get_time();
            int64_t window_us = window_now_us - perf_window_start_us;
            float fps = (window_us > 0) ? ((float)FRAME_LOG_INTERVAL * 1000000.0f / (float)window_us) : 0.0f;
            float changed_fps = (window_us > 0) ? ((float)changed_count * 1000000.0f / (float)window_us) : 0.0f;
            float avg_advance_ms = (float)advance_total_us / (float)FRAME_LOG_INTERVAL / 1000.0f;
            float avg_blit_ms = (changed_count > 0) ? ((float)blit_total_us / (float)changed_count / 1000.0f) : 0.0f;
            float avg_flush_wait_ms = (changed_count > 0) ? ((float)flush_wait_total_us / (float)changed_count / 1000.0f) : 0.0f;
            size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
            ESP_LOGI(TAG,
                     "stats: fps=%.1f changed_fps=%.1f avg_adv=%.2fms avg_blit=%.2fms avg_flush_wait=%.2fms free_internal=%u free_psram=%u",
                     fps,
                     changed_fps,
                     avg_advance_ms,
                     avg_blit_ms,
                     avg_flush_wait_ms,
                     (unsigned)free_internal,
                     (unsigned)free_psram);
            perf_window_start_us = window_now_us;
            changed_count = 0;
            advance_total_us = 0;
            blit_total_us = 0;
            flush_wait_total_us = 0;
        }

        const int64_t target_frame_us = 1000000LL / TARGET_FPS;
        int64_t frame_end_us = esp_timer_get_time();
        int64_t frame_elapsed_us = frame_end_us - now_us;
        int64_t sleep_us = target_frame_us - frame_elapsed_us;
        if (sleep_us > 0) {
            TickType_t sleep_ticks = pdMS_TO_TICKS((uint32_t)((sleep_us + 999) / 1000));
            if (sleep_ticks > 0) {
                vTaskDelay(sleep_ticks);
            }
        }
    }
}
