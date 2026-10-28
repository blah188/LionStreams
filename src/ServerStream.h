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
// more is sent. On ESP32 the socket is then closed right away, by Finish() -- not
// out of impatience with a slow client, but because the framework writes its OWN
// terminating chunk after the handler returns, where this class can no longer
// protect anything (see SERVERSTREAM_CLOSE_ON_CUT). Where that macro is off, the
// socket is left alone: the web server closes it in its own loop, and a
// live-but-slow client is never killed off by this class.
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

// Close a cut client in Finish() instead of leaving it to the web server (1 = on, 0 = off).
//
// This is not impatience with a slow peer. A cut page is already truncated, and the
// terminating chunk is deliberately not sent to a client that stopped reading -- but
// skipping it only MOVES that write, it does not remove it. The response is chunked, so
// once the handler returns the framework runs its own finalizer
// (WebServer::_finalizeResponse() -> sendContent("")) and pushes "0\r\n\r\n" into the same
// socket through an unguarded write. Measured on a real hub twice in two days (21.10.2026):
// 23 s and 30 s of a frozen loop for those three bytes, with the page itself long finished.
//
// Nothing in this class can reach that write -- it happens after the last line the page
// controls -- so the only lever left is the early return inside the core's own write:
// `if (!_connected || fd < 0) return 0;`. Closing the socket here arms it, and the
// framework's finalizer costs nothing. Verified present in both ESP32 cores 2.0.17 and
// 3.2.x, which also still reset the write retry counter on every partial send.
//
// ESP32 only by default: the ESP8266 core times each write against the client timeout and
// bails out at once on a CLOSED connection, so there the leftover write costs a few seconds
// at worst -- and closing early would truncate a page that a slow-but-live client would
// have finished reading.
#ifndef SERVERSTREAM_CLOSE_ON_CUT
#if defined(ESP32)
#define SERVERSTREAM_CLOSE_ON_CUT 1
#else
#define SERVERSTREAM_CLOSE_ON_CUT 0
#endif
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
          _budgetMs(budgetMs), _spentMs(0), _muted(false), _finished(false)
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

    // End of the page: flush what is still buffered and close the chunked response,
    // unless the stream was cut. Returns FALSE when it was — the caller then knows the
    // page never reached the client and can log it, which is otherwise invisible.
    //
    // A cut page also closes the client socket here, unless SERVERSTREAM_CLOSE_ON_CUT is
    // off (it is on for ESP32 and off for ESP8266 by default). That is what keeps the
    // framework's own terminating chunk from blocking the caller for half a minute; the
    // macro's comment has the measurements.
    //
    // Replaces the pair every consumer used to write by hand:
    //     out.flush();
    //     server.chunkedResponseFinalize();   // or sendContent("") on ESP32
    // Both halves were easy to get wrong. The terminating chunk must NOT be sent to a
    // client that stopped reading — it is one more write into a socket that already
    // refuses them — and which call terminates a response is a platform detail no page
    // should have to know. The old pair still works; this is the shorter, safer way.
    //
    // Idempotent: calling it twice will not emit a second terminator (a stray one lands
    // after the response is complete and corrupts the next request on a keep-alive
    // connection).
    bool Finish()
    {
        if (_finished)
            return !_muted;
        _finished = true;
        doFlush(); // sends the last partial chunk, or discards it if muted
        if (_muted)
        {
#if SERVERSTREAM_CLOSE_ON_CUT
            // Our terminating chunk is skipped below, but the framework still has one of
            // its own to send once we return — into a socket that is not draining. Close
            // the client and that write turns into an early `return 0`.
            _server.client().stop();
#endif
            return false;
        }
#ifdef ESP32
        _server.sendContent(""); // ESP32 WebServer has no chunkedResponseFinalize()
#else
        _server.chunkedResponseFinalize();
#endif
        return true;
    }

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
        // Only before the FIRST chunk, and that is the whole point of this probe: the
        // budget is measured inside sendContent(), so on the first chunk nothing has been
        // spent yet and the budget cannot protect anything. It guards against a single
        // write() that never returns (WiFiClient::write() loops on select() with a 1 s
        // timeout and 10 retries, but ANY partial send resets that counter, so a peer
        // draining a few bytes at a time holds the caller indefinitely — 30 s on 724
        // bytes, measured).
        //
        // It used to run before EVERY chunk, and that was wrong: select() answers "is
        // there room in the send buffer right now", not "is the client alive". A healthy
        // browser fills that buffer routinely in the middle of a page — we push chunks
        // faster than TCP drains them — and the stream was then declared dead, dropping
        // the tail and the terminating chunk. Invisible from the browser (the visible
        // HTML had already arrived, and a page that refreshes replaces the hung request),
        // but real: 17 cut pages out of ~660 requests overnight on one node. From the
        // second chunk on, the budget already covers a slow peer, and it is the honest
        // measure — five seconds actually spent writing, not one 300 ms guess.
        else if (_spentMs == 0 && !clientWritable(_server))
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
    bool _finished; // Finish() already ran — never emit a second terminating chunk
#ifdef SERVERSTREAM_TRACE
    unsigned _chunks;
#endif
};

#endif
