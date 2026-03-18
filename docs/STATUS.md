# Project Status

## What Is Working

- Core lifecycle API: `rive_thorvg_init`, `rive_create`, `rive_load(_ex)`, `rive_advance`, `rive_render`, `rive_destroy`
- State machine input APIs: bool/number/trigger by name
- ViewModel/Data Binding APIs: set/get bool/number/string/color/enum, trigger firing
- Pointer plumbing: pointer down/move/up and screen-to-artboard conversion
- Vector rendering: paths, fills, strokes, gradients, clip paths
- Image rendering: embedded PNG/JPEG/WebP decode through `stb_image` + WebP decoder
- Image mesh rendering: software triangle rasterization + deferred composition
- Asset loading callback bridge: external/referenced assets via `rive_set_asset_loader`
- Desktop SDL example: loads `.riv` files, pointer interaction, input introspection
- ESP32 direct example (Waveshare 1.46B): display + touch wired, flashable with ESP-IDF

## In Progress / WIP

- Text Tier 1+ (`stb_truetype` / full shaping path): not implemented yet
- Rive event callback extraction and dispatch after state machine advance
- Broader automated validation (deterministic golden tests and expanded CI matrix)
- Expanded board coverage beyond the current Waveshare ESP32-S3 target

## Known Limits

- Text is currently stubbed (invisible)
- Luau scripting content is not supported
- Full international text shaping stack is not integrated
- ESP32 example is tuned to one board and one default embedded asset (`assets/loader.riv`)

## Recently Validated Assets

- `assets/loader.riv` on `examples/esp32_direct`
- `assets/mesh.riv` (mesh textures and blending path)
