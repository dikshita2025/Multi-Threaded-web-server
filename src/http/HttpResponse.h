#pragma once

#include <string>
#include <unordered_map>
#include <memory>

// ─────────────────────────────────────────────────────────────────────────────
// HttpResponse
//
// Represents an HTTP Response to be sent back to the client.
// Implements a simple Builder pattern.
// ─────────────────────────────────────────────────────────────────────────────
class HttpResponse {
public:
    explicit HttpResponse(int status_code = 200, const std::string& status_text = "OK");

    // Builder methods
    HttpResponse& set_status(int code, const std::string& text);
    HttpResponse& set_header(const std::string& key, const std::string& value);
    HttpResponse& set_body(const std::string& body);
    HttpResponse& set_body(std::string&& body);
    HttpResponse& set_cached_body(std::shared_ptr<const std::string> body);
    HttpResponse& set_version(const std::string& version);
    HttpResponse& set_file(const std::string& path, size_t size);

    // Serialization
    std::string serialize() const;
    std::string serialize_headers() const; // Serializes status line + headers (with trailing CRLF)

    // Getters
    int status_code() const { return status_code_; }
    const std::string& status_text() const { return status_text_; }
    const std::string& body() const { return body_; }
    const std::string& file_path() const { return file_path_; }
    size_t file_size() const { return file_size_; }
    bool is_file() const { return !file_path_.empty(); }
    std::shared_ptr<const std::string> cached_body() const { return cached_body_; }

    // Helpers for common responses
    static HttpResponse make_200(const std::string& body, const std::string& content_type = "text/html; charset=utf-8");
    static HttpResponse make_304(); // Not Modified
    static HttpResponse make_400(const std::string& msg = "Bad Request");
    static HttpResponse make_403(const std::string& msg = "Forbidden");
    static HttpResponse resolve_error(int status_code);

private:
    int status_code_{ 200 };
    std::string status_text_{ "OK" };
    std::string version_{ "HTTP/1.1" };
    std::string body_;
    std::shared_ptr<const std::string> cached_body_;
    std::string file_path_;
    size_t file_size_{ 0 };
    std::unordered_map<std::string, std::string> headers_;
};
