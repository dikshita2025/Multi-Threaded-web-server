#pragma once

#include <string>
#include <unordered_map>

// ─────────────────────────────────────────────────────────────────────────────
// MimeTypes
//
// Simple registry mapping file extensions to MIME type strings.
// ─────────────────────────────────────────────────────────────────────────────
class MimeTypes {
public:
    static std::string get_type(const std::string& path) {
        size_t dot = path.find_last_of('.');
        if (dot == std::string::npos) {
            return "application/octet-stream"; // default binary stream
        }

        std::string ext = path.substr(dot);
        // Convert to lowercase
        for (char& c : ext) {
            c = std::tolower(static_cast<unsigned char>(c));
        }

        static const std::unordered_map<std::string, std::string> mime_map = {
            { ".html", "text/html; charset=utf-8" },
            { ".htm",  "text/html; charset=utf-8" },
            { ".css",  "text/css" },
            { ".js",   "application/javascript" },
            { ".json", "application/json" },
            { ".xml",  "application/xml" },
            { ".png",  "image/png" },
            { ".jpg",  "image/jpeg" },
            { ".jpeg", "image/jpeg" },
            { ".gif",  "image/gif" },
            { ".svg",  "image/svg+xml" },
            { ".ico",  "image/x-icon" },
            { ".txt",  "text/plain; charset=utf-8" },
            { ".pdf",  "application/pdf" },
            { ".zip",  "application/zip" }
        };

        auto it = mime_map.find(ext);
        if (it != mime_map.end()) {
            return it->second;
        }

        return "application/octet-stream";
    }
};
