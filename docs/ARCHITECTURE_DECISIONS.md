# Architecture Decisions

This document records the key technical decisions currently shaping the repository.

## ADR-001: Keep a C API on top of C++ internals

- Status: accepted
- Decision: expose a stable C interface in `include/rive_thorvg.h`, while keeping the runtime and renderer bridge in C++.
- Why: easier integration into ESP-IDF/LVGL projects and C-heavy firmware codebases.
- Consequences: C++ complexity stays internal; ABI surface remains small and explicit.

## ADR-002: Use ThorVG software rendering as the backend

- Status: accepted
- Decision: implement `rive::Factory` and `rive::Renderer` using ThorVG SW canvas.
- Why: no GPU dependency, portable behavior across desktop + MCU targets.
- Consequences: performance depends on art complexity and CPU budget; design constraints are important.

## ADR-003: Keep ViewModel/Data Binding support

- Status: accepted
- Decision: include ViewModel/data-binding runtime and expose VM set/get APIs.
- Why: modern Rive files commonly rely on data binding, not only classic state machine inputs.
- Consequences: larger binary than a minimal renderer, but much better `.riv` compatibility.

## ADR-004: Defer mesh image composition to post-canvas blit

- Status: accepted
- Decision: rasterize image meshes in software and blend them into the staging buffer after `canvas->draw()/sync()`.
- Why: avoids ThorVG picture composition edge cases for textured meshes on embedded targets.
- Consequences: predictable mesh output, with CPU cost proportional to mesh/image complexity.

## ADR-005: Default text mode is Tier 0 (stub/invisible)

- Status: accepted (current default)
- Decision: compile with `src/text/stub_text_engine.cpp` by default; text rendering is disabled.
- Why: keep footprint and dependencies low for embedded targets.
- Consequences: `.riv` files with text will load, but text is not visible until higher text tiers are implemented.

## ADR-006: Trim heavy runtime subsystems

- Status: accepted
- Decision: exclude runtime audio engine, Luau scripting, command queue/server paths in build selection.
- Why: reduce complexity and dependency footprint for MCU deployment.
- Consequences: scripting-driven content is not supported; audio events are not full playback integration.

## ADR-007: ESP32 direct example uses half-res render + 2x nearest upscale

- Status: accepted
- Decision: render at `206x206` ARGB8888 (`RENDER_SCALE=2`) and upscale to `412x412` RGB565 with 2x pixel duplication.
- Why: better frame-time headroom on ESP32-S3 than full-res rendering.
- Consequences: lower visual fidelity than full-res/bilinear paths, but stable performance.

## ADR-008: Keep ESP-IDF component in-tree for reproducibility

- Status: accepted
- Decision: `examples/esp32_direct/components/rive_thorvg/` builds the bridge/runtime/ThorVG sources directly.
- Why: deterministic builds for hardware bring-up and easier contributor onboarding.
- Consequences: longer component CMake file, but fewer external integration surprises.
