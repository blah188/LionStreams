# LionStreams

Two tiny, header-only Arduino `Stream` adapters for embedded C++:

- **`StringStream`** — a read/write `Stream` view over an external `String`.
- **`ServerStream`** — a write-only `Stream` that flushes `Print` output to a
  **synchronous** web server in chunks, so large HTML pages stream out without
  holding the whole response in RAM.

Both are non-copyable and have **no dependencies** beyond the Arduino core.

## Install

PlatformIO (`platformio.ini`):

```ini
lib_deps = leva/LionStreams
```

Arduino IDE: Library Manager → search **LionStreams**.

Supported: **ESP8266** and **ESP32** (Arduino framework). `StringStream` itself is
core-only and portable; `ServerStream` needs an ESP web server.

## StringStream

A `Stream` whose backing store is a `String` **you own**.

```cpp
#include <LionStreams.h>

String buffer;
StringStream out(buffer);
out.printf("temp=%d", 21);   // appends to `buffer`  → "temp=21"

// read side (independent cursor; does not modify the String):
while (out.available())
    Serial.write((char)out.read());

out.reset();                 // rewinds the read cursor AND clears `buffer`
```

> ⚠️ **Contract**
> - **Non-owning.** It stores a *reference* to your `String`; that `String` must
>   **outlive** the stream. The stream never frees it.
> - **Writing appends**; reading consumes through an internal cursor.
> - **`reset()` clears the referenced `String`** (and rewinds the cursor).
> - **Non-copyable** (copy/assignment deleted) — a stream aliasing one `String`
>   can't be sensibly copied.

## ServerStream

Generate big pages incrementally. Instead of building the whole HTML in one
`String`, write it through a `ServerStream`; it flushes a chunk every `flushSize`
bytes (default `1024`) via `server.sendContent()`.

```cpp
void handleRoot() {
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);  // chunked response
    server.send(200, "text/html", "");                // start it

    ServerStream out(server);                         // or ServerStream(server, 2048)
    out.print(F("<html><body><ol>"));
    for (int i = 0; i < 500; i++)
        out.printf("<li>%d</li>", i);                 // auto-flushes as it fills
    out.print(F("</ol></body></html>"));
    out.flush();                                      // send the last chunk
}
```

> ⚠️ **Contract**
> - **You must start the response first** (`setContentLength(CONTENT_LENGTH_UNKNOWN)`
>   + `send(200, type, "")`) before writing.
> - **Call `flush()` at the end.** The destructor does **not** flush — a stack-local
>   stream is destroyed at method exit, and silently emitting a partial buffer there
>   (e.g. on an error/early-return path) would be surprising, so flushing is explicit.
> - **Write-only**: `read()`/`peek()` return `-1`, `available()` returns `0`.
> - **Non-copyable.**
> - Server type is selected per platform: `ESP8266WebServer` on ESP8266,
>   `WebServer` (the synchronous one) on ESP32.

### ESPAsyncWebServer?

You don't need a wrapper. `ESPAsyncWebServer` already gives you a streaming
`Print`/`Stream`:

```cpp
AsyncResponseStream *r = request->beginResponseStream("text/html");
r->printf("<html>...%d...</html>", value);
request->send(r);
```

## API

**StringStream(String &s)** — `available()` · `read()` · `peek()` · `write()` ·
`print*()`/`printf()` (append) · `reset()` (clear + rewind).

**ServerStream(server, flushSize = 1024)** — `write()` · `print*()`/`printf()`
(buffered, auto-flush at `flushSize`) · `flush()`.

## License

0BSD — see [LICENSE](LICENSE).
