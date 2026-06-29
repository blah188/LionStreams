# Changelog

All notable changes to this project are documented here.
This project adheres to [Semantic Versioning](https://semver.org/).

## [1.0.0] - 2026-06-29

First public release. Extracted and hardened from a private monorepo.

### Added
- `StringStream` — read/write Arduino `Stream` view over an external `String`.
- `ServerStream` — write-only `Stream` that buffers `Print` output and flushes it
  in chunks via `server.sendContent()`.
- `LionStreams.h` umbrella header.
- Example `LionStreamsBasic` and Arduino IDE keyword highlighting.

### Changed
- `ServerStream` now works on both ESP8266 (`ESP8266WebServer`) and ESP32
  (synchronous `WebServer`) via a platform-selected server type, instead of being
  hard-wired to `ESP8266WebServer`.
- Both classes are now explicitly **non-copyable** (deleted copy/assignment) and
  carry their ownership/usage contract in the header.

### Fixed
- `StringStream::reset()` AVR guard was `#ifdef AVR` (never defined) — corrected to
  `#if defined(__AVR__)`, so the AVR-safe path (`string = F("")`) is actually taken
  on AVR instead of calling `String::clear()` (absent on classic AVR cores).
