# Running Examples

## Prerequisites

- Clone with submodules:

  ```bash
  git clone --recurse-submodules <repo-url>
  ```

- Tooling:
  - CMake 3.16+
  - C++17-capable compiler
  - SDL2 (for desktop example)
  - ESP-IDF (for ESP32 example)

## Desktop SDL Example

Build from repository root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DRIVE_THORVG_BUILD_SDL=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Run:

```bash
./build/examples/desktop_sdl/rive_sdl_example assets/loader.riv
./build/examples/desktop_sdl/rive_sdl_example assets/vehicles.riv
./build/examples/desktop_sdl/rive_sdl_example assets/vehicles.riv Jeep
```

Controls:

- Mouse move: pointer move
- Mouse down/up: pointer down/up
- `Space`: fire all discovered trigger inputs
- `B`: toggle all discovered boolean inputs
- `N`: fire VM trigger `Next`
- `P`: fire VM trigger `Back`
- `Esc`: quit

## ESP32 Direct Example (No LVGL)

Target board in this repo: Waveshare `ESP32-S3-Touch-LCD-1.46B`.

Build and flash:

```bash
cd examples/esp32_direct
. "$HOME/esp/esp-idf/export.sh"
idf.py build
idf.py -p /dev/<your-port> flash
```

Optional serial monitor:

```bash
idf.py -p /dev/<your-port> monitor
```

Current embedded asset in the example:

- `assets/loader.riv` (configured in `examples/esp32_direct/main/CMakeLists.txt`)

## Changing the Embedded `.riv` Asset

1. Update `EMBED_FILES` in `examples/esp32_direct/main/CMakeLists.txt`
2. Update embed symbols in `examples/esp32_direct/main/main.c` to match
3. Rebuild + flash:

```bash
idf.py build
idf.py -p /dev/<your-port> flash
```

## Troubleshooting

- If submodule checks fail: run `git submodule update --init --recursive`
- If SDL build is skipped: install SDL2 and re-run CMake configure
- If ESP32 flash fails with "port busy": close monitor/serial tools using the same `/dev` port
