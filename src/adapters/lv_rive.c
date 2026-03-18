/// lv_rive.c — LVGL widget adapter for rive-thorvg.
///
/// Wraps rive-thorvg as an LVGL widget (~150 lines).
/// - Allocates a pixel buffer (PSRAM on ESP32, heap otherwise)
/// - Creates a rive_instance_t pointing to that buffer
/// - Registers an lv_timer that calls rive_advance() and invalidates the widget
/// - Forwards LVGL touch events to rive_pointer_* with coordinate conversion
/// - Exposes the buffer as an lv_image_dsc_t for LVGL to draw

#include "lv_rive.h"

#include <stdlib.h>
#include <string.h>

// ESP32 PSRAM allocation (fallback to regular malloc on desktop)
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#define RIVE_ALLOC(size) heap_caps_malloc(size, MALLOC_CAP_SPIRAM)
#define RIVE_FREE(ptr)   free(ptr)
#else
#define RIVE_ALLOC(size) malloc(size)
#define RIVE_FREE(ptr)   free(ptr)
#endif

// ---------------------------------------------------------------------------
// Internal widget data
// ---------------------------------------------------------------------------

typedef struct {
    lv_obj_t obj;                   // Must be first — LVGL widget inheritance
    rive_instance_t* rive;          // Rive instance
    void* pixel_buffer;             // Owned pixel buffer (PSRAM)
    int buf_width;
    int buf_height;
    lv_image_dsc_t img_dsc;         // Image descriptor for LVGL drawing
    lv_timer_t* timer;              // Advance timer
    uint32_t last_tick;             // Last advance timestamp
    int target_fps;                 // Target FPS (default 30)
} lv_rive_t;

// Forward declarations
static void lv_rive_constructor(const lv_obj_class_t* class_p, lv_obj_t* obj);
static void lv_rive_destructor(const lv_obj_class_t* class_p, lv_obj_t* obj);
static void lv_rive_event_cb(lv_event_t* e);
static void lv_rive_timer_cb(lv_timer_t* timer);

// ---------------------------------------------------------------------------
// Widget class definition
// ---------------------------------------------------------------------------

const lv_obj_class_t lv_rive_class = {
    .constructor_cb = lv_rive_constructor,
    .destructor_cb = lv_rive_destructor,
    .instance_size = sizeof(lv_rive_t),
    .base_class = &lv_obj_class,
    .name = "rive",
};

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

static void lv_rive_constructor(const lv_obj_class_t* class_p, lv_obj_t* obj) {
    LV_UNUSED(class_p);
    lv_rive_t* rive_obj = (lv_rive_t*)obj;
    rive_obj->rive = NULL;
    rive_obj->pixel_buffer = NULL;
    rive_obj->buf_width = 0;
    rive_obj->buf_height = 0;
    rive_obj->timer = NULL;
    rive_obj->last_tick = 0;
    rive_obj->target_fps = 30;

    // Register event handler for drawing and input
    lv_obj_add_event_cb(obj, lv_rive_event_cb, LV_EVENT_ALL, NULL);
}

static void lv_rive_destructor(const lv_obj_class_t* class_p, lv_obj_t* obj) {
    LV_UNUSED(class_p);
    lv_rive_t* rive_obj = (lv_rive_t*)obj;

    if (rive_obj->timer) {
        lv_timer_delete(rive_obj->timer);
        rive_obj->timer = NULL;
    }

    if (rive_obj->rive) {
        rive_destroy(rive_obj->rive);
        rive_obj->rive = NULL;
    }

    if (rive_obj->pixel_buffer) {
        RIVE_FREE(rive_obj->pixel_buffer);
        rive_obj->pixel_buffer = NULL;
    }
}

// ---------------------------------------------------------------------------
// Event handler — drawing and touch forwarding
// ---------------------------------------------------------------------------

static void lv_rive_event_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* obj = lv_event_get_target(e);
    lv_rive_t* rive_obj = (lv_rive_t*)obj;

    if (code == LV_EVENT_DRAW_MAIN) {
        if (!rive_obj->rive || !rive_obj->pixel_buffer) return;

        lv_layer_t* layer = lv_event_get_layer(e);
        lv_area_t coords;
        lv_obj_get_coords(obj, &coords);

        // Draw the Rive pixel buffer as an image
        lv_draw_image_dsc_t draw_dsc;
        lv_draw_image_dsc_init(&draw_dsc);
        draw_dsc.src = &rive_obj->img_dsc;

        lv_draw_image(layer, &draw_dsc, &coords);
    }
    else if (code == LV_EVENT_PRESSING || code == LV_EVENT_PRESSED) {
        if (!rive_obj->rive) return;
        lv_indev_t* indev = lv_indev_active();
        if (!indev) return;

        lv_point_t point;
        lv_indev_get_point(indev, &point);

        lv_area_t coords;
        lv_obj_get_coords(obj, &coords);

        float sx = (float)(point.x - coords.x1);
        float sy = (float)(point.y - coords.y1);
        float ax, ay;
        rive_screen_to_artboard(rive_obj->rive, sx, sy, &ax, &ay);

        if (code == LV_EVENT_PRESSED) {
            rive_pointer_down(rive_obj->rive, ax, ay);
        } else {
            rive_pointer_move(rive_obj->rive, ax, ay);
        }
    }
    else if (code == LV_EVENT_RELEASED) {
        if (!rive_obj->rive) return;
        lv_indev_t* indev = lv_indev_active();
        if (!indev) return;

        lv_point_t point;
        lv_indev_get_point(indev, &point);

        lv_area_t coords;
        lv_obj_get_coords(obj, &coords);

        float sx = (float)(point.x - coords.x1);
        float sy = (float)(point.y - coords.y1);
        float ax, ay;
        rive_screen_to_artboard(rive_obj->rive, sx, sy, &ax, &ay);
        rive_pointer_up(rive_obj->rive, ax, ay);
    }
}

// ---------------------------------------------------------------------------
// Timer callback — advance animation and invalidate widget
// ---------------------------------------------------------------------------

static void lv_rive_timer_cb(lv_timer_t* timer) {
    lv_obj_t* obj = (lv_obj_t*)lv_timer_get_user_data(timer);
    lv_rive_t* rive_obj = (lv_rive_t*)obj;

    if (!rive_obj->rive) return;

    uint32_t now = lv_tick_get();
    float elapsed = (now - rive_obj->last_tick) / 1000.0f;
    rive_obj->last_tick = now;

    // Cap elapsed to prevent large jumps
    if (elapsed > 0.1f) elapsed = 0.1f;

    bool changed = rive_advance(rive_obj->rive, elapsed);
    if (changed) {
        lv_obj_invalidate(obj);
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

lv_obj_t* lv_rive_create(lv_obj_t* parent, int width, int height) {
    lv_obj_t* obj = lv_obj_class_create_obj(&lv_rive_class, parent);
    lv_obj_class_init_obj(obj);

    lv_rive_t* rive_obj = (lv_rive_t*)obj;

    // Allocate pixel buffer (RGB565 — 2 bytes per pixel)
    size_t buf_size = (size_t)width * height * 2;
    rive_obj->pixel_buffer = RIVE_ALLOC(buf_size);
    if (!rive_obj->pixel_buffer) return obj;  // Allocation failed

    memset(rive_obj->pixel_buffer, 0, buf_size);
    rive_obj->buf_width = width;
    rive_obj->buf_height = height;

    // Create Rive instance
    rive_obj->rive = rive_create(rive_obj->pixel_buffer, width, height,
                                 RIVE_PIXEL_FORMAT_RGB565);

    // Set up image descriptor for LVGL drawing
    memset(&rive_obj->img_dsc, 0, sizeof(rive_obj->img_dsc));
    rive_obj->img_dsc.header.w = width;
    rive_obj->img_dsc.header.h = height;
    rive_obj->img_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    rive_obj->img_dsc.data = (const uint8_t*)rive_obj->pixel_buffer;
    rive_obj->img_dsc.data_size = buf_size;

    // Set widget size
    lv_obj_set_size(obj, width, height);

    // Create advance timer (default 30fps = 33ms period)
    rive_obj->last_tick = lv_tick_get();
    rive_obj->timer = lv_timer_create(lv_rive_timer_cb, 1000 / 30, obj);

    return obj;
}

void lv_rive_set_src(lv_obj_t* obj, const uint8_t* riv_data, size_t riv_len) {
    lv_rive_t* rive_obj = (lv_rive_t*)obj;
    if (!rive_obj->rive) return;
    rive_load(rive_obj->rive, riv_data, riv_len);
}

void lv_rive_set_bool(lv_obj_t* obj, const char* name, bool value) {
    lv_rive_t* rive_obj = (lv_rive_t*)obj;
    if (!rive_obj->rive) return;
    rive_set_bool(rive_obj->rive, name, value);
}

void lv_rive_set_number(lv_obj_t* obj, const char* name, float value) {
    lv_rive_t* rive_obj = (lv_rive_t*)obj;
    if (!rive_obj->rive) return;
    rive_set_number(rive_obj->rive, name, value);
}

void lv_rive_fire_trigger(lv_obj_t* obj, const char* name) {
    lv_rive_t* rive_obj = (lv_rive_t*)obj;
    if (!rive_obj->rive) return;
    rive_fire_trigger(rive_obj->rive, name);
}

void lv_rive_vm_set_bool(lv_obj_t* obj, const char* path, bool value) {
    lv_rive_t* rive_obj = (lv_rive_t*)obj;
    if (!rive_obj->rive) return;
    rive_vm_set_bool(rive_obj->rive, path, value);
}

void lv_rive_vm_set_number(lv_obj_t* obj, const char* path, float value) {
    lv_rive_t* rive_obj = (lv_rive_t*)obj;
    if (!rive_obj->rive) return;
    rive_vm_set_number(rive_obj->rive, path, value);
}

void lv_rive_vm_set_string(lv_obj_t* obj, const char* path, const char* value) {
    lv_rive_t* rive_obj = (lv_rive_t*)obj;
    if (!rive_obj->rive) return;
    rive_vm_set_string(rive_obj->rive, path, value);
}

void lv_rive_vm_set_color(lv_obj_t* obj, const char* path, uint32_t argb) {
    lv_rive_t* rive_obj = (lv_rive_t*)obj;
    if (!rive_obj->rive) return;
    rive_vm_set_color(rive_obj->rive, path, argb);
}

void lv_rive_vm_set_enum(lv_obj_t* obj, const char* path, const char* value) {
    lv_rive_t* rive_obj = (lv_rive_t*)obj;
    if (!rive_obj->rive) return;
    rive_vm_set_enum(rive_obj->rive, path, value);
}

void lv_rive_set_fit(lv_obj_t* obj, rive_fit_t fit, rive_align_t align) {
    lv_rive_t* rive_obj = (lv_rive_t*)obj;
    if (!rive_obj->rive) return;
    rive_set_fit(rive_obj->rive, fit, align);
}

void lv_rive_set_fps(lv_obj_t* obj, int fps) {
    lv_rive_t* rive_obj = (lv_rive_t*)obj;
    if (fps < 1) fps = 1;
    if (fps > 120) fps = 120;
    rive_obj->target_fps = fps;
    if (rive_obj->timer) {
        lv_timer_set_period(rive_obj->timer, 1000 / fps);
    }
}

void lv_rive_set_event_callback(lv_obj_t* obj, rive_event_cb cb,
                                void* user_data) {
    lv_rive_t* rive_obj = (lv_rive_t*)obj;
    if (!rive_obj->rive) return;
    rive_set_event_callback(rive_obj->rive, cb, user_data);
}

rive_instance_t* lv_rive_get_instance(lv_obj_t* obj) {
    lv_rive_t* rive_obj = (lv_rive_t*)obj;
    return rive_obj->rive;
}
