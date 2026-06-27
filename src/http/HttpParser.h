#pragma once

#include "http/HttpRequest.h"
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// HttpParser
//
// A stateful HTTP request parser that supports partial reads and keep-alive
// connections. It parses an incoming raw buffer and updates an HttpRequest.
// ─────────────────────────────────────────────────────────────────────────────
class HttpParser {
public:
    enum class ParseStatus {
        COMPLETE,     // Request fully parsed
        INCOMPLETE,   // Need more data (call parse() again with new data)
        ERROR         // Protocol error (malformed request)
    };

    HttpParser();

    // Resets the parser state so it can be reused for the next request on the same connection.
    void reset();

    // Feeds new data from the socket into the parser's internal buffer,
    // and attempts to parse it. Updates `request`.
    ParseStatus parse(HttpRequest& request, const char* data, size_t length);

private:
    enum class State {
        REQUEST_LINE,
        HEADERS,
        BODY,
        COMPLETE,
        ERROR
    };

    ParseStatus parse_loop(HttpRequest& request);
    ParseStatus parse_request_line(HttpRequest& request, const std::string& line);
    ParseStatus parse_header(HttpRequest& request, const std::string& line);

    State state_{ State::REQUEST_LINE };
    std::string buffer_;
    size_t body_bytes_read_{ 0 };
    size_t content_length_{ 0 };
};
