#ifndef _LION_SERVER_STREAM_H_
#define _LION_SERVER_STREAM_H_

// ServerStream — a write-only Arduino Stream that buffers Print output and
// flushes it to a synchronous web server via sendContent(), so large pages
// can be generated incrementally without holding the whole response in RAM.
//
// Works on:
//   * ESP8266 — ESP8266WebServer
//   * ESP32   — WebServer (the synchronous one, <WebServer.h>)
// For the asynchronous ESPAsyncWebServer use AsyncServerStream instead.
//
// Contract / usage:
//   * Before writing, start a chunked / unknown-length response yourself, e.g.
//       server.setContentLength(CONTENT_LENGTH_UNKNOWN);
//       server.send(200, "text/html", "");
//   * Write your page through this stream (print/printf/write). It auto-flushes
//     a chunk every `flushSize` bytes (default 1024).
//   * Call flush() at the end so the last (sub-flushSize) chunk is sent.
//     The destructor does NOT flush: a stack-local stream is destroyed only at
//     method exit, and silently emitting a partial buffer there (e.g. on an
//     error/early-return path) would be surprising. Flushing stays explicit.
//   * WRITE-ONLY: read()/peek() return -1, available() returns 0.
//   * NON-COPYABLE: it holds a server reference and a partial buffer; copying
//     is meaningless, so the copy/assignment operators are deleted.

#include <Arduino.h>
#include <Stream.h>

#if defined(ESP8266)
#include <ESP8266WebServer.h>
using ServerStreamServer = ESP8266WebServer;
#elif defined(ESP32)
#include <WebServer.h>
using ServerStreamServer = WebServer;
#else
#error "ServerStream supports ESP8266 and ESP32 (synchronous WebServer). For ESPAsyncWebServer use AsyncServerStream."
#endif

class ServerStream : public Stream
{
public:
    static const size_t DEFAULT_FLUSH_SIZE = 1024;

    explicit ServerStream(ServerStreamServer &srv, size_t flushSize = DEFAULT_FLUSH_SIZE)
        : _server(srv), _flushSize(flushSize ? flushSize : DEFAULT_FLUSH_SIZE)
    {
        _buffer.reserve(_flushSize);
    }

    ServerStream(const ServerStream &) = delete;
    ServerStream &operator=(const ServerStream &) = delete;

    size_t write(const uint8_t *buffer, size_t size) override
    {
        _buffer.concat(reinterpret_cast<const char *>(buffer), size);
        if (_buffer.length() >= _flushSize)
            doFlush();
        return size;
    }

    size_t write(uint8_t c) override
    {
        _buffer += (char)c;
        if (_buffer.length() >= _flushSize)
            doFlush();
        return 1;
    }

    void flush() override { doFlush(); }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }

private:
    void doFlush()
    {
        if (_buffer.length())
        {
            _server.sendContent(_buffer);
            _buffer.clear();
        }
    }

    ServerStreamServer &_server;
    String _buffer;
    size_t _flushSize;
};

#endif
