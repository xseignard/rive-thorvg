# Contributing

Thanks for helping improve `rive-thorvg`.

## Development Setup

1. Clone with submodules:

   ```bash
   git clone --recurse-submodules <repo-url>
   ```

2. Build desktop example:

   ```bash
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DRIVE_THORVG_BUILD_SDL=ON
   cmake --build build -j
   ```

3. Run a quick smoke check:

   ```bash
   ./build/examples/desktop_sdl/rive_sdl_example assets/loader.riv
   ```

4. Run automated smoke test:

   ```bash
   ctest --test-dir build --output-on-failure
   ```

## Coding Guidelines

- Keep the C API in `include/rive_thorvg.h` stable and backward compatible.
- Prefer small, focused changes with clear commit messages.
- Avoid adding runtime logs in hot render paths.
- Match existing style and naming in touched files.
- Keep embedded-facing defaults conservative on memory/perf.

## Pull Request Checklist

- Build passes locally for the desktop target.
- CTest smoke checks pass locally.
- If you changed ESP32 code, `examples/esp32_direct` still builds with ESP-IDF.
- Documentation is updated when behavior or defaults change.
- New warnings/logs are intentional and minimal.
- No generated artifacts or local machine files are committed.

## Scope Notes

- Text is currently Tier 0 (stub/invisible) unless extended in a dedicated change.
- Luau scripting is currently out of scope.
- The Waveshare ESP32-S3 1.46B board is the main validated embedded target.
