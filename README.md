# rive-thorvg

`rive-thorvg` is a lightweight C API around the Rive C++ runtime, backed by ThorVG's software renderer.

It targets embedded devices (ESP32-S3 in this repo) and also includes a desktop SDL example for fast iteration.

## Highlights

- C-first API in `include/rive_thorvg.h`
- Rive state machine inputs + ViewModel/Data Binding support
- Vector rendering, gradients, clipping, images, and image meshes
- ThorVG dependency baseline pinned to `v1.0.2`
- Optional LVGL adapter (`src/adapters/lv_rive.c`)
- ESP-IDF example for Waveshare ESP32-S3 Touch LCD 1.46B

## Quick Start

1. Clone with submodules:

   ```bash
   git clone --recurse-submodules <repo-url>
   ```

2. Build desktop SDL example:

   ```bash
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DRIVE_THORVG_BUILD_SDL=ON
   cmake --build build -j
   ./build/examples/desktop_sdl/rive_sdl_example assets/loader.riv
   ```

3. Build ESP32 direct example:

   ```bash
   cd examples/esp32_direct
   . "$HOME/esp/esp-idf/export.sh"
   idf.py build
   idf.py -p /dev/<your-port> flash
   ```

## Documentation

- Architecture decisions: `docs/ARCHITECTURE_DECISIONS.md`
- Current status (working + WIP): `docs/STATUS.md`
- How to run examples: `docs/RUNNING_EXAMPLES.md`
- Design guidelines (current pipeline): `docs/DESIGN_GUIDELINES.md`
- Remaining work / roadmap: `docs/REMAINING_WORK.md`
- Contribution guide: `CONTRIBUTING.md`

## Community & Release

- Changelog: `CHANGELOG.md`
- Security policy: `SECURITY.md`
- Automated releases: `.github/workflows/release-please.yml`

## Repository Layout

- `include/` public headers
- `src/` core C API + ThorVG bridge + adapters
- `examples/desktop_sdl/` desktop runner for quick validation
- `examples/esp32_direct/` ESP-IDF hardware example
- `assets/` test/demo `.riv` files
- `docs/` project documentation

## License

This repository is MIT licensed (`LICENSE`).
