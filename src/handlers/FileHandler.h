#pragma once

#include "http/HttpRequest.h"
#include "http/HttpResponse.h"
#include "cache/LruCache.h"
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// FileHandler
//
// Maps HTTP GET/HEAD requests to filesystem static assets.
// Features:
//   - Directory indexing (e.g. "/" or "/sub/" -> "/index.html")
//   - Path Traversal prevention (security checks)
//   - If-Modified-Since check (HTTP 304 caching)
//   - Custom error page loading if error files exist
// ─────────────────────────────────────────────────────────────────────────────
class FileHandler {
public:
    explicit FileHandler(std::string www_root);

    // Handles the request and returns an HttpResponse (possibly a file response)
    HttpResponse handle(const HttpRequest& req);

private:
    // Checks path for ".." traversal tricks
    bool is_safe_path(const std::string& path) const;

    // Helper to format UNIX timestamp to IMF-fixdate format (RFC 7231)
    static std::string format_http_date(time_t time);

    std::string www_root_;
    LruCache<std::string, std::string> cache_;
};
