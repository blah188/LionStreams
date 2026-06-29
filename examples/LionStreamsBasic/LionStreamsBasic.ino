// LionStreams basic example.
//
//   StringStream  — build a String through a Stream/Print interface, then read it back.
//   ServerStream  — stream a large HTML page to a web client in chunks, without
//                   holding the whole page in RAM.
//
// Works on ESP8266 (ESP8266WebServer) and ESP32 (synchronous WebServer).
// Open http://<device-ip>/ in a browser to see the streamed page.

#include <LionStreams.h>

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
ESP8266WebServer server(80);
#elif defined(ESP32)
#include <WiFi.h>
#include <WebServer.h>
WebServer server(80);
#endif

// Override from build_flags, e.g.  -D WIFI_SSID=\"myssid\" -D WIFI_PASS=\"mypass\"
#ifndef WIFI_SSID
#define WIFI_SSID "your-ssid"
#endif
#ifndef WIFI_PASS
#define WIFI_PASS "your-pass"
#endif

// --- StringStream demo: build a String via Print, then read it char-by-char ---
void demoStringStream()
{
    String buffer;
    StringStream ss(buffer);

    ss.print(F("temp="));
    ss.print(21.5, 1);
    ss.print(F("C"));
    // `buffer` now holds "temp=21.5C"
    Serial.print(F("StringStream built: "));
    Serial.println(buffer);

    // Read side: consume what we wrote
    Serial.print(F("Read back: "));
    while (ss.available())
        Serial.write((char)ss.read());
    Serial.println();
}

// --- ServerStream demo: stream a long page in chunks ---
void handleRoot()
{
    server.setContentLength(CONTENT_LENGTH_UNKNOWN); // chunked transfer
    server.send(200, "text/html", "");               // start the response

    ServerStream out(server); // flushes every 1024 bytes by default
    out.print(F("<html><body><h1>LionStreams</h1><ol>"));
    for (int i = 1; i <= 200; i++)
        out.printf("<li>row %d</li>", i); // auto-flushes as the buffer fills
    out.print(F("</ol></body></html>"));
    out.flush(); // send the final (sub-threshold) chunk
}

void setup()
{
    Serial.begin(115200);
    delay(200);

    demoStringStream();

    Serial.println();
    Serial.print(F("WiFi SSID : "));
    Serial.println(WIFI_SSID);
    Serial.print(F("WiFi pass : "));
    Serial.println(strlen(WIFI_PASS) ? F("(set)") : F("(empty)"));
    Serial.print(F("MAC       : "));
    Serial.println(WiFi.macAddress());

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print(F("Connecting"));
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 20000)
    {
        delay(500);
        Serial.print('.');
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.print(F("Connected. IP: "));
        Serial.print(WiFi.localIP());
        Serial.print(F("  RSSI: "));
        Serial.println(WiFi.RSSI());
    }
    else
    {
        Serial.print(F("FAILED to connect. WiFi.status() = "));
        Serial.println((int)WiFi.status());
        Serial.println(F("status codes: 0=IDLE 1=NO_SSID_AVAIL 3=CONNECTED "
                         "4=CONNECT_FAILED 5=CONNECTION_LOST 6=WRONG_PASSWORD 7=DISCONNECTED"));
        return; // skip starting the server
    }

    server.on("/", handleRoot);
    server.begin();
    Serial.println(F("HTTP server started — open http://<IP>/"));
}

void loop()
{
    server.handleClient();
}
