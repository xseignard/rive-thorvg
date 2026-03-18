# ESP32 Direct Example (No LVGL)

Bare ESP-IDF app that runs `rive_thorvg` directly and flushes RGB565 frames to the panel.

Target board: Waveshare `ESP32-S3-Touch-LCD-1.46B` (412x412 round display).

## Current behavior

- Render loop uses `rive_advance()` and flushes only on changed frames.
- Display bring-up is board-specific (`main/board_waveshare_1_46b.c`).
- Touch input comes from SPD2010 over I2C and drives Rive inputs/triggers.
- Current embedded asset: `assets/loader.riv`.
- Touch panel is initialized and sampled each frame.
- Pointer event forwarding hooks exist in firmware and can be enabled via `ENABLE_POINTER_EVENTS`.
- Output path is half-res ARGB render (`206x206`) + 2x nearest-like upscale to `412x412` RGB565.

## Build and flash

```bash
cd examples/esp32_direct
. "$HOME/esp/esp-idf/export.sh"
idf.py build
idf.py -p /dev/<your-port> flash
```

Optional monitor:

```bash
idf.py -p /dev/<your-port> monitor
```

## Pin mapping

The board mapping is prefilled in `main/main.c`:

- LCD QSPI: `D0=46 D1=45 D2=42 D3=41 SCK=40 CS=21 TE=18 BL=5`
- Shared I2C: `SCL=10 SDA=11`
- Touch INT: `4`
- `LCD_RST` and `TP_RST` routed via TCA9554 expander (`EXIO2/EXIO1`)

## Changing the embedded `.riv`

1. Update `EMBED_FILES` in `main/CMakeLists.txt`
2. Update `*_riv_start/end` symbols in `main/main.c`
3. Rebuild and flash

## Tuning Notes

- Touch orientation toggles are in `main/board_waveshare_1_46b.c`:
  - `WS_TOUCH_SWAP_XY`
  - `WS_TOUCH_INVERT_X`
  - `WS_TOUCH_INVERT_Y`
- Performance stats are logged every `FRAME_LOG_INTERVAL` frames (`avg_adv`, `avg_blit`, `avg_flush_wait`).
