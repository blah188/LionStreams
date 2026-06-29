#ifndef _SERVER_STREAM_H_
#define _SERVER_STREAM_H_

#include <Stream.h>
#include <ESP8266WebServer.h>

class ServerStream : public Stream
{
public:
    static const size_t DEFAULT_FLUSH_SIZE = 1024;

    ServerStream(ESP8266WebServer &srv, size_t flushSize = DEFAULT_FLUSH_SIZE)
        : _server(srv), _flushSize(flushSize)
    {
        _buffer.reserve(_flushSize);
    }

    virtual size_t write(const uint8_t *buffer, size_t size) override
    {
        _buffer.concat(reinterpret_cast<const char*>(buffer), size);
        if (_buffer.length() >= _flushSize)
            doFlush();
        return size;
    }

    virtual size_t write(uint8_t c) override
    {
        _buffer += (char)c;
        if (_buffer.length() >= _flushSize)
            doFlush();
        return 1;
    }

    void flush() override { doFlush(); }
    virtual int available() { return 0; }
    virtual int read() { return -1; }
    virtual int peek() { return -1; }

private:
    void doFlush()
    {
        if (_buffer.length()) {
            _server.sendContent(_buffer);
            _buffer.clear();
        }
    }

    ESP8266WebServer &_server;
    String _buffer;
    size_t _flushSize;
};

#endif
