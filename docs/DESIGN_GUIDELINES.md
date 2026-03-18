# Rive Design Guidelines (Current ESP32 Pipeline)

These guidelines reflect the current `examples/esp32_direct` setup in this repository.

## Runtime Pipeline Snapshot

- Display output: `412x412` RGB565
- Render target: `206x206` ARGB8888 (`RENDER_SCALE=2`)
- Upscale path: 2x pixel duplication / nearest-like blit
- Renderer: Rive runtime + ThorVG software backend
- Meshes: software-rasterized image meshes with deferred composition

Goal: keep stable ~30 FPS with room for touch/input handling.

## Authoring Baseline

1. Prefer artboards close to `206x206` for this profile.
2. Keep state-machine logic rich, but visible geometry per frame moderate.
3. Treat this target as fill-rate and overdraw constrained, not color-count constrained.

## Biggest Performance Costs

1. High visible path count and dense curve geometry.
2. Heavy overdraw from overlapping translucent layers.
3. Large animated regions that invalidate most pixels every frame.
4. Many simultaneously visible textured meshes.
5. Frequent full-screen blended effects.

## Texture and Image Guidance

Because the final step is nearest-like 2x upscaling:

1. Avoid tiny texture details that rely on subpixel smoothing.
2. Prefer slightly larger source textures for critical visual areas.
3. Keep alpha edges clean to reduce shimmer/aliasing.
4. Use texture detail where it matters; keep secondary regions simpler.

## State Machine / Input Naming

1. Input names are case-sensitive (`IsHovering` is different from `isHovering`).
2. Keep naming consistent between Rive editor and firmware constants.
3. Keep interaction input names explicit and stable across assets and firmware.

## Practical Design Rules

1. Reuse shapes/components instead of duplicating unique paths.
2. Use gradients intentionally; avoid gradient-heavy full-screen motion.
3. Keep long-running background animation subtle and low-cost.
4. Localize motion to key interactive zones when possible.

## Runtime Validation Targets

Use the periodic stats logs from `examples/esp32_direct/main/main.c`:

- `avg_adv`: animation + render cost
- `avg_blit`: upscale + format conversion cost
- `avg_flush_wait`: wait for async display flush completion

For healthy 30 FPS operation:

- Target `avg_adv <= 20ms` (ideal: `<= 15ms`)
- Keep `avg_blit` bounded and stable
- Keep `avg_flush_wait` close to zero during steady state

If frame time regresses, reduce visible complexity first (paths/overdraw/mesh count).

## Reference Assets in This Repo

- `assets/loader.riv`: current default for ESP32 direct example
- `assets/mesh.riv`: useful for validating mesh texture rendering path
