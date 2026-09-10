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
//
// Why a time budget (SERVERSTREAM_PAGE_BUDGET_MS)
// -----------------------------------------------
// A chunk is one socket write(), and a client that stops reading makes that
// write() block. The two cores differ, and the ESP32 one has no ceiling at all:
//
//   * ESP8266 (ClientContext::_write_from_source) starts a timer once per
//     write() and gives up after the client timeout (WebServer sets 5 s), and
//     bails out immediately once the connection is CLOSED. Hard ceiling.
//   * ESP32 (WiFiClient::write) loops on select() with a 1 s timeout and
//     WIFI_CLIENT_MAX_WRITE_RETRY (10) attempts -- but every partial send
//     RESETS the retry counter. A peer that drains a few bytes at a time keeps
//     one write() spinning indefinitely: page renders stalling the caller for
//     20-80 s were measured on a real hub. Both constants are plain #defines
//     inside the framework's .cpp, so they cannot be overridden.
//
// Hence this stream stops feeding a socket that is not keeping up: once the
// page has spent longer than its budget INSIDE sendContent(), or the peer has
// reset the connection, the stream goes MUTE -- write() still accepts and counts
// everything, so the caller finishes generating the page normally, but nothing
// more is sent. The socket is NOT closed here: the web server closes it in its
// own loop, and a live-but-slow client is never killed off by this class.
//
// Only time spent inside sendContent() is charged to the budget, so a page that
// is slow to GENERATE (reading files, walking a long device list) never loses
// its reader. A healthy client drains a page in milliseconds, which puts the
// budget out of reach in normal operation. Pass 0 as budgetMs to disable it and
// keep only the connection check.

#include <Arduino.h>
#include <Stream.h>

// Page time budget in milliseconds, 0 disables it. Override before including.
//
// Default ON for ESP32 and OFF for ESP8266, deliberately: the 8266 framework already caps
// a write (ClientContext::_write_from_source times the whole write against the client
// timeout, 5 s under ESP8266WebServer, and bails out at once when the connection is
// CLOSED). There the budget would add nothing but a risk — a slow yet perfectly alive
// client would get a truncated page where it used to get a whole one. On ESP32 there is no
// cap at all, and an unguarded page render was measured stalling the caller for 190 s.
#ifndef SERVERSTREAM_PAGE_BUDGET_MS
#if defined(ESP32)
#define SERVERSTREAM_PAGE_BUDGET_MS 5000ul
#else
#define SERVERSTREAM_PAGE_BUDGET_MS 0ul
#endif
#endif

// With SERVERSTREAM_TRACE, only chunks at least this slow are printed to Serial.
#ifndef SERVERSTREAM_TRACE_MS
#define SERVERSTREAM_TRACE_MS 200ul
#endif

// ESP32 only: how long to wait for the socket to become writable before giving up on
// the page. The budget above cannot cover the FIRST chunk -- nothing has been spent yet
// when it is written -- and a single chunk of 724 bytes was measured stalling for 30 s.
// So ask the socket whether there is room BEFORE entering the blocking write. A healthy
// client frees window space in microseconds; this wait only ever expires on one that has
// stopped reading altogether.
#ifndef SERVERSTREAM_WRITE_WAIT_MS
#define SERVERSTREAM_WRITE_WAIT_MS 300
#endif

#if defined(ESP8266)
#include <ESP8266WebServer.h>
using ServerStreamServer = ESP8266WebServer;
#elif defined(ESP32)
#include <WebServer.h>
#include <sys/select.h> // socket write-readiness probe, see SERVERSTREAM_WRITE_WAIT_MS
using ServerStreamServer = WebServer;
#else
#error "ServerStream supports ESP8266 and ESP32 (synchronous WebServer). For ESPAsyncWebServer use AsyncServerStream."
#endif

class ServerStream : public Stream
{
public:
    static const size_t DEFAULT_FLUSH_SIZE = 1024;
    static const uint32_t DEFAULT_PAGE_BUDGET_MS = SERVERSTREAM_PAGE_BUDGET_MS;

    explicit ServerStream(ServerStreamServer &srv, size_t flushSize = DEFAULT_FLUSH_SIZE,
                          uint32_t budgetMs = DEFAULT_PAGE_BUDGET_MS)
        : _server(srv), _flushSize(flushSize ? flushSize : DEFAULT_FLUSH_SIZE),
          _budgetMs(budgetMs), _spentMs(0), _muted(false)
#ifdef SERVERSTREAM_TRACE
          , _chunks(0)
#endif
    {
        _buffer.reserve(_flushSize);
    }

    ServerStream(const ServerStream &) = delete;
    ServerStream &operator=(const ServerStream &) = delete;

    // Is there room in the server client's send window right now? Public and static because
    // the first write of a response does NOT go through this stream: the headers leave via
    // server.send()/send_P(), and on ESP32 that single write has no ceiling either. A caller
    // can therefore ask this before starting a response at all:
    //     if (!ServerStream::clientWritable(server)) return;
    //
    // This is a READINESS probe, not a liveness test: nothing is read, nothing is closed,
    // errno is untouched. (Do not reach for WiFiClient::connected() instead — it probes with
    // a zero-length recv() that returns 0 without setting errno, so the framework judges by
    // whatever errno an unrelated call left behind and declares healthy clients dead.)
    // Always true on ESP8266, where a write cannot outlast the client timeout anyway.
    static bool clientWritable(ServerStreamServer &srv)
    {
#if defined(ESP32) && SERVERSTREAM_WRITE_WAIT_MS > 0
        const int fd = srv.client().fd();
        if (fd < 0)
            return false;

        fd_set wset;
        FD_ZERO(&wset);
        FD_SET(fd, &wset);
        struct timeval tv;
        tv.tv_sec = SERVERSTREAM_WRITE_WAIT_MS / 1000;
        tv.tv_usec = (SERVERSTREAM_WRITE_WAIT_MS % 1000) * 1000;
        // > 0 also covers a socket in error: then the write fails immediately, which is
        // fine — what must be avoided is a socket that is merely full.
        return select(fd + 1, NULL, &wset, NULL, &tv) > 0 && FD_ISSET(fd, &wset);
#else
        (void)srv;
        return true;
#endif
    }

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

    // True once the stream gave up on this client (the send budget is spent).
    // Everything written from now on is discarded; the caller may use this to
    // stop generating an expensive page early, but does not have to.
    bool aborted() const { return _muted; }

private:
    void doFlush()
    {
        if (!_buffer.length())
            return;

        if (muted())
        {
            _buffer.clear();
            return;
        }

        // Only the time spent inside sendContent() counts against the budget:
        // that is exactly the price of a blocked socket write. Page generation
        // itself may legitimately be slow (reading files, walking a device list)
        // and must not cost a healthy client its page.
        const uint32_t before = millis();
        const size_t bytes = _buffer.length();
        _server.sendContent(_buffer);
        const uint32_t took = (uint32_t)(millis() - before);
        _spentMs += took;
        _buffer.clear();
#ifdef SERVERSTREAM_TRACE
        _chunks++;
        // Only slow chunks: a healthy client takes a millisecond or two per chunk,
        // and printing every one of them buries the interesting lines.
        if (took >= SERVERSTREAM_TRACE_MS)
            Serial.printf_P(PSTR("  chunk %u (%u B): %lu ms, spent %lu\n"), _chunks, (unsigned)bytes,
                            (unsigned long)took, (unsigned long)_spentMs);
#else
        (void)bytes;
#endif
    }

    bool muted()
    {
        if (_muted)
            return true;

        // DO NOT add a client().connected() check here. On ESP32 it is not a
        // usable liveness test: WiFiClient::connected() probes the socket with
        // recv(fd, &dummy, 0, MSG_DONTWAIT), and a zero-length recv returns 0
        // WITHOUT touching errno -- the framework then decides from whatever
        // errno some earlier, unrelated operation left behind (typically
        // ECONNRESET from a previous failed write) and reports "disconnected"
        // for a perfectly healthy client. Tried it: every page went out as
        // "200 OK" with zero bytes of body.
        //
        // The time budget needs no such guess: a client that is not draining
        // the response is exactly a client whose writes are slow.
        if (_budgetMs && _spentMs > _budgetMs)
        {
            _muted = true;
#ifdef SERVERSTREAM_TRACE
            Serial.printf_P(PSTR("  MUTED after %u chunks, %lu ms spent\n"), _chunks,
                            (unsigned long)_spentMs);
#endif
        }
#if defined(ESP32) && SERVERSTREAM_WRITE_WAIT_MS > 0
        // Ask before every chunk, not just the first: this is the only defence against a
        // single write() that never returns (WiFiClient::write() loops on select() with a
        // 1 s timeout and 10 retries, but ANY partial send resets that counter, so a peer
        // draining a few bytes at a time holds the caller indefinitely — 30 s on 724 bytes,
        // measured). The budget cannot cover the FIRST chunk: nothing is spent yet.
        else if (!clientWritable(_server))
        {
            _muted = true;
#ifdef SERVERSTREAM_TRACE
            Serial.printf_P(PSTR("  MUTED at chunk %u: socket not writable\n"), _chunks + 1);
#endif
        }
#endif

        return _muted;
    }

    ServerStreamServer &_server;
    String _buffer;
    size_t _flushSize;
    uint32_t _budgetMs;
    uint32_t _spentMs; // cumulative time spent inside sendContent()
    bool _muted;
#ifdef SERVERSTREAM_TRACE
    unsigned _chunks;
#endif
};

#endif
