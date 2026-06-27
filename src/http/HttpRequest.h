#pragma once

#include <string>
#include <unordered_map>

// ─────────────────────────────────────────────────────────────────────────────
// HttpRequest
//
// Represents a parsed HTTP Request.
// Includes method, path, HTTP version, headers, query parameters, and body.
// Header lookups are case-insensitive.
// ─────────────────────────────────────────────────────────────────────────────
class HttpRequest {
public:
    HttpRequest() = default;

    // Setters
    void set_method(const std::string& method) { method_ = method; }
    void set_path(const std::string& path) { path_ = path; }
    void set_version(const std::string& version) { version_ = version; }
    void set_body(const std::string& body) { body_ = body; }
    void set_query_string(const std::string& query) { query_string_ = query; }
    void add_header(std::string key, const std::string& value);

    // Getters
    const std::string& method() const { return method_; }
    const std::string& path() const { return path_; }
    const std::string& query_string() const { return query_string_; }
    const std::string& version() const { return version_; }
    const std::string& body() const { return body_; }
    
    // Checks if a header exists (case-insensitive)
    bool has_header(const std::string& key) const;
    
    // Gets a header value (case-insensitive), returns empty string if not found
    std::string get_header(const std::string& key) const;

    // Checks if "Connection: keep-alive" is active
    bool keep_alive() const;

private:
    // Helper to convert string to lowercase
    static std::string to_lower(std::string str);

    std::string method_;
    std::string path_;
    std::string query_string_;
    std::string version_;
    std::string body_;
    std::unordered_map<std::string, std::string> headers_; // stored with lowercase keys
};
