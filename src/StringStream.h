#ifndef _STRING_STREAM_H_
#define _STRING_STREAM_H_

#include <Stream.h>

class StringStream : public Stream
{
public:
    StringStream(String &s) : string(s), position(0) {}

    // Stream methods
    virtual int available() { return string.length() - position; }
    virtual int read() { return position < string.length() ? string[position++] : -1; }
    virtual int peek() { return position < string.length() ? string[position] : -1; }
    virtual void flush(){};
    // Print methods
    virtual size_t write(uint8_t c)
    {
        string += (char)c;
        return 1;
    };

    virtual size_t write(const uint8_t *buffer, size_t size)
    {
        string.concat(reinterpret_cast<const char*>(buffer), size);
        return size;
    }

    // Special
    void reset()
    {
        position = 0;
#ifdef AVR
        string = F("");
#else
        string.clear();
#endif        
    }

private:
    String &string;
    unsigned int position;
};

#endif