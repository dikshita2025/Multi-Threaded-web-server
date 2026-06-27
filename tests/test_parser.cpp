#include "http/HttpParser.h"
#include "http/HttpRequest.h"
#include <iostream>
#include <cassert>

void test_basic_get() {
    HttpParser parser;
    HttpRequest req;
    std::string raw = "GET /index.html HTTP/1.1\r\n"
                      "Host: localhost:8080\r\n"
                      "Connection: close\r\n"
                      "\r\n";
    
    auto status = parser.parse(req, raw.data(), raw.size());
    (void)status;
    assert(status == HttpParser::ParseStatus::COMPLETE);
    assert(req.method() == "GET");
    assert(req.path() == "/index.html");
    assert(req.version() == "HTTP/1.1");
    assert(req.get_header("Host") == "localhost:8080");
    assert(req.get_header("connection") == "close"); // Case-insensitivity test
    assert(!req.keep_alive());
    std::cout << "test_basic_get passed\n";
}

void test_url_decoding() {
    HttpParser parser;
    HttpRequest req;
    std::string raw = "GET /search?q=c%2B%2B%20programming%20tutorial HTTP/1.1\r\n"
                      "\r\n";
    auto status = parser.parse(req, raw.data(), raw.size());
    (void)status;
    assert(status == HttpParser::ParseStatus::COMPLETE);
    assert(req.path() == "/search");
    assert(req.query_string() == "q=c++ programming tutorial");
    std::cout << "test_url_decoding passed\n";
}

void test_post_with_body() {
    HttpParser parser;
    HttpRequest req;
    std::string raw = "POST /api/submit HTTP/1.1\r\n"
                      "Content-Length: 15\r\n"
                      "Content-Type: application/json\r\n"
                      "\r\n"
                      "{\"value\": \"foo\"}";
    auto status = parser.parse(req, raw.data(), raw.size());
    (void)status;
    assert(status == HttpParser::ParseStatus::COMPLETE);
    assert(req.method() == "POST");
    assert(req.body() == "{\"value\": \"foo\"}");
    assert(req.get_header("Content-Type") == "application/json");
    std::cout << "test_post_with_body passed\n";
}

void test_fragmented_request() {
    HttpParser parser;
    HttpRequest req;
    std::string part1 = "GET /index.html HT";
    std::string part2 = "TP/1.1\r\nHost: local";
    std::string part3 = "host\r\n\r\n";

    auto s1 = parser.parse(req, part1.data(), part1.size());
    (void)s1;
    assert(s1 == HttpParser::ParseStatus::INCOMPLETE);

    auto s2 = parser.parse(req, part2.data(), part2.size());
    (void)s2;
    assert(s2 == HttpParser::ParseStatus::INCOMPLETE);

    auto s3 = parser.parse(req, part3.data(), part3.size());
    (void)s3;
    assert(s3 == HttpParser::ParseStatus::COMPLETE);

    assert(req.method() == "GET");
    assert(req.path() == "/index.html");
    assert(req.get_header("Host") == "localhost");
    std::cout << "test_fragmented_request passed\n";
}

void test_malformed_request() {
    HttpParser parser;
    HttpRequest req;
    std::string raw = "GET /index.html\r\n\r\n"; // Missing HTTP version
    auto status = parser.parse(req, raw.data(), raw.size());
    (void)status;
    assert(status == HttpParser::ParseStatus::ERROR);
    std::cout << "test_malformed_request passed\n";
}

int main() {
    std::cout << "Running HttpParser tests...\n";
    test_basic_get();
    test_url_decoding();
    test_post_with_body();
    test_fragmented_request();
    test_malformed_request();
    std::cout << "All parser tests passed successfully!\n";
    return 0;
}
