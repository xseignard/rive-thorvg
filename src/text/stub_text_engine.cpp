/// Tier 0: Stub text engine.
///
/// .riv files with text runs load successfully but text is invisible.
/// Factory::decodeFont() returns a stub font object so nothing crashes —
/// text shapes simply produce empty paths.
///
/// This is the shipping default for rive-thorvg. Designers who target
/// this library either avoid Rive text or pre-render text as images
/// within the Rive file. Zero code, zero cost.

// TODO: Once rive-runtime is integrated, this file will provide:
//
// 1. A StubFont class implementing rive::Font that returns empty paths
//    for all glyphs and produces no shaped output.
//
// 2. A factory integration point so that TvgFactory::decodeFont()
//    returns a StubFont rather than nullptr, preventing crashes on
//    .riv files that contain text content.
//
// The stub must:
// - Return valid (but empty) RawPath from getPath()
// - Return zero-length glyph runs from shape()
// - Report valid metrics (ascent/descent) so layout doesn't crash
// - Be a compile-time drop-in replacement for stb_text_engine.cpp (Tier 1)
//
// Compile-time selection:
//   #if !defined(RIVE_THORVG_STB_TEXT) && !defined(RIVE_THORVG_HARFBUZZ)
//     // This file is active
//   #endif
