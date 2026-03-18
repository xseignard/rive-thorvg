# Remaining Work

This is the current actionable backlog to move the repository from "working prototype" to mature open-source project quality.

## 1) Runtime Features

- Implement and ship Text Tier 1 (`stb_truetype`) behind `RIVE_THORVG_STB_TEXT`
- Add a real event dispatch path for `rive_set_event_callback`
- Validate additional blend/image edge cases for complex mesh scenes

## 2) Example Hardening

- Add asset selection mode for `examples/esp32_direct` (build-time option or Kconfig)
- Add one more verified ESP32 board profile or a clearer board abstraction layer
- Add performance presets (`RENDER_SCALE`, target FPS, worker threads) with documented tradeoffs

## 3) Testing and CI

- Add deterministic golden-image tests for core rendering paths
- Expand smoke coverage beyond the current render checksum test
- Add CI jobs for:
  - macOS desktop build

## 4) Documentation and DX

- Add a short API cookbook with copy-paste snippets
- Add a capability-by-asset compatibility matrix (specific tested `.riv` files)

## 5) Packaging

- Add a reusable CMake package export/install story for downstream consumers
- Provide an ESP-IDF component packaging path outside `examples/esp32_direct`
