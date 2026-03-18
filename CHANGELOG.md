# Changelog

All notable changes to this project are documented in this file.

The format is based on Keep a Changelog and this project follows Semantic Versioning.

## [0.2.0](https://github.com/xseignard/rive-thorvg/compare/v0.1.0...v0.2.0) (2026-03-18)


### Features

* bootstrap rive-thorvg open-source baseline ([5a95873](https://github.com/xseignard/rive-thorvg/commit/5a958739c49df7e15cfc98bbcc9e1d075fd82956))


### Bug Fixes

* **ci:** source esp-idf before esp32 build ([3ddddb9](https://github.com/xseignard/rive-thorvg/commit/3ddddb9ec6c05299b69012d872d2fc1e1da1b72a))

## [Unreleased]

### Added

- CI workflow for desktop build/tests and ESP32 compile-only checks.
- Core release docs (`SECURITY.md`, `CONTRIBUTING.md`, `CHANGELOG.md`).
- Render smoke test (`tests/render_smoke.c`) wired through CTest.
- Automated release PR/tagging workflow via `release-please`.

### Changed

- ESP32 default asset baseline set to `assets/loader.riv`.
- ESP32 direct example touch loop made asset-agnostic (no hardcoded loader-specific SM inputs).

## [0.1.0] - 2026-03-18

### Added

- Initial public release of `rive-thorvg`.
- C API wrapper around Rive runtime + ThorVG software backend.
- Desktop SDL example and ESP32 direct example.
- State machine + ViewModel APIs, image and image mesh rendering support.
