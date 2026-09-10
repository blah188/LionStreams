# Changelog

All notable changes to this project are documented here.
This project adheres to [Semantic Versioning](https://semver.org/).

## [1.1.0] - 2026-09-10

### Added
- `ServerStream` now gives up on a client that stops draining the response instead of
  stalling the caller. Once the page has spent more than `SERVERSTREAM_PAGE_BUDGET_MS`
  *inside `sendContent()`* — slow page generation is not charged to the budget — the stream
  goes mute: `write()` keeps accepting and counting bytes, so the caller finishes the page
  normally, but nothing more is sent. The socket is never closed by this class — the web
  server closes it in its own loop, and a live-but-slow client is not killed off.

  The budget defaults to 5000 ms **on ESP32 and 0 (off) on ESP8266**: the 8266 framework
  already caps a write at the client timeout and bails out at once on a closed connection,
  so there a budget would add no protection and only risk truncating a page for a
  slow-but-alive client. Override the macro to opt in or out on either core.
- **Write-readiness probe (ESP32, `SERVERSTREAM_WRITE_WAIT_MS`, default 300 ms).** The
  budget alone cannot cover the FIRST chunk — nothing has been spent when it is written —
  and one 724-byte chunk was measured stalling for 30 s, because ANY partial send resets
  the framework's retry counter. So before each chunk the stream asks `select()` whether
  the socket has window space, with its own short deadline, and mutes instead of entering
  a write that may never return. A healthy client frees window space in microseconds.
  This is a readiness probe, not a liveness test: nothing is read, nothing is closed and
  `errno` is left alone.
- `ServerStream::aborted()` reports that state, so a caller may stop generating
  an expensive page early.
- **`ServerStream::clientWritable(server)`** — the same readiness probe as a public static
  method, because the first write of a response never goes through the stream: the headers
  leave via `server.send()`/`send_P()`, and on ESP32 that single write has no ceiling
  either. A page handler can now bail out before starting a response at all. Always true on
  ESP8266.

  Deliberately NOT part of this: a `client().connected()` check. On ESP32 that
  call is not a liveness test — it probes with a zero-length `recv()`, which
  returns 0 without setting `errno`, and the framework then judges by whatever
  `errno` an earlier unrelated operation left behind. With a stale `ECONNRESET`
  in place it declares a healthy client disconnected, and every page goes out as
  `200 OK` with an empty body. Measured, not theorised.

  Background: a chunk is one socket `write()`. On ESP8266 the framework caps a
  write at the client timeout (5 s under `WebServer`) and bails out at once on a
  closed connection. On ESP32 `WiFiClient::write()` loops on `select()` with a
  1 s timeout and 10 retries, but **every partial send resets the retry
  counter** — a peer draining a few bytes at a time keeps one write spinning
  indefinitely (20-80 s page renders measured on real hardware). Both framework
  constants are plain `#define`s in a .cpp and cannot be overridden, so the
  ceiling has to live here.

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
