#ifndef _LION_STREAMS_H_
#define _LION_STREAMS_H_

// LionStreams — small Arduino Stream adapters around String / web servers.
//
//   StringStream  — read/write Stream view over an external String (portable).
//   ServerStream  — write-only Stream that flushes Print output to a synchronous
//                   web server (ESP8266WebServer / ESP32 WebServer) in chunks,
//                   so big pages are generated without holding them in RAM.
//
// For ESPAsyncWebServer (ESP32) you don't need a wrapper: request->beginResponseStream()
// already returns an AsyncResponseStream that IS a Print/Stream — write to it directly.

#include "StringStream.h"

#if defined(ESP8266) || defined(ESP32)
#include "ServerStream.h"
#endif

#endif
