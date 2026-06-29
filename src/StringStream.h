#ifndef _LION_STRING_STREAM_H_
#define _LION_STRING_STREAM_H_

// StringStream — a read/write Arduino Stream view over an external String.
//
// Contract:
//   * NON-OWNING: holds a reference to the String you pass in. That String
//     MUST outlive the StringStream. The StringStream never frees it.
//   * Writing (print/printf/write) APPENDS to the referenced String.
//   * Reading (read/peek/available) consumes the String through an internal
//     position cursor; it does not modify the String.
//   * reset() rewinds the read cursor AND clears the referenced String.
//   * NON-COPYABLE: copying a stream that aliases one String is ambiguous,
//     so the copy/assignment operators are deleted by design.

#include <Arduino.h>
#include <Stream.h>

class StringStream : public Stream
{
public:
    explicit StringStream(String &s) : string(s), position(0) {}

    StringStream(const StringStream &) = delete;
    StringStream &operator=(const StringStream &) = delete;

    // Stream (read side)
    int available() override { return (int)(string.length() - position); }
    int read() override { return position < string.length() ? string[position++] : -1; }
    int peek() override { return position < string.length() ? string[position] : -1; }
    void flush() override {}

    // Print (write side) — appends to the referenced String
    size_t write(uint8_t c) override
    {
        string += (char)c;
        return 1;
    }

    size_t write(const uint8_t *buffer, size_t size) override
    {
        string.concat(reinterpret_cast<const char *>(buffer), size);
        return size;
    }

    // Rewind the read cursor and clear the referenced String.
    void reset()
    {
        position = 0;
#if defined(__AVR__)
        string = F(""); // classic AVR String has no clear()
#else
        string.clear();
#endif
    }

private:
    String &string;
    unsigned int position;
};

#endif
