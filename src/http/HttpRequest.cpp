#include "http/HttpRequest.h"
#include <algorithm>
#include <cctype>

void HttpRequest::add_header(std::string key, const std::string& value) {
    headers_[to_lower(std::move(key))] = value;
}

bool HttpRequest::has_header(const std::string& key) const {
    return headers_.find(to_lower(key)) != headers_.end();
}

std::string HttpRequest::get_header(const std::string& key) const {
    auto it = headers_.find(to_lower(key));
    if (it != headers_.end()) {
        return it->second;
    }
    return "";
}

bool HttpRequest::keep_alive() const {
    std::string conn = get_header("Connection");
    if (version_ == "HTTP/1.1") {
        // HTTP/1.1 defaults to keep-alive unless explicitly closed
        return conn != "close";
    } else {
        // HTTP/1.0 defaults to close unless keep-alive is explicitly requested
        return conn == "keep-alive";
    }
}

std::string HttpRequest::to_lower(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) {
        return std::tolower(c);
    });
    return str;
}
