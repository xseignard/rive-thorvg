#ifndef LV_RIVE_H
#define LV_RIVE_H

#include "lvgl.h"
#include "rive_thorvg.h"

#ifdef __cplusplus
extern "C" {
#endif

// Create a Rive widget. Allocates its own pixel buffer in PSRAM.
// Defaults to RIVE_PIXEL_FORMAT_RGB565 (native for most LVGL displays).
lv_obj_t* lv_rive_create(lv_obj_t* parent, int width, int height);

// Load a .riv file into the widget.
void lv_rive_set_src(lv_obj_t* obj, const uint8_t* riv_data, size_t riv_len);

// State machine inputs.
void lv_rive_set_bool(lv_obj_t* obj, const char* name, bool value);
void lv_rive_set_number(lv_obj_t* obj, const char* name, float value);
void lv_rive_fire_trigger(lv_obj_t* obj, const char* name);

// ViewModel data binding.
void lv_rive_vm_set_bool(lv_obj_t* obj, const char* path, bool value);
void lv_rive_vm_set_number(lv_obj_t* obj, const char* path, float value);
void lv_rive_vm_set_string(lv_obj_t* obj, const char* path, const char* value);
void lv_rive_vm_set_color(lv_obj_t* obj, const char* path, uint32_t argb);
void lv_rive_vm_set_enum(lv_obj_t* obj, const char* path, const char* value);

// Fit and alignment.
void lv_rive_set_fit(lv_obj_t* obj, rive_fit_t fit, rive_align_t align);

// Set target FPS (default 30). Controls the internal LVGL timer period.
void lv_rive_set_fps(lv_obj_t* obj, int fps);

// Set event callback for audio events etc.
void lv_rive_set_event_callback(lv_obj_t* obj, rive_event_cb cb, void* user_data);

// Access the underlying instance (for advanced use).
rive_instance_t* lv_rive_get_instance(lv_obj_t* obj);

#ifdef __cplusplus
}
#endif

#endif // LV_RIVE_H
