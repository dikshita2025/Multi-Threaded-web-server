#include "http/HttpResponse.h"
#include <sstream>

HttpResponse::HttpResponse(int status_code, const std::string& status_text)
    : status_code_(status_code), status_text_(status_text) {}

HttpResponse& HttpResponse::set_status(int code, const std::string& text) {
    status_code_ = code;
    status_text_ = text;
    return *this;
}

HttpResponse& HttpResponse::set_header(const std::string& key, const std::string& value) {
    headers_[key] = value;
    return *this;
}

HttpResponse& HttpResponse::set_body(const std::string& body) {
    body_ = body;
    set_header("Content-Length", std::to_string(body_.size()));
    return *this;
}

HttpResponse& HttpResponse::set_body(std::string&& body) {
    body_ = std::move(body);
    set_header("Content-Length", std::to_string(body_.size()));
    return *this;
}

HttpResponse& HttpResponse::set_version(const std::string& version) {
    version_ = version;
    return *this;
}

HttpResponse& HttpResponse::set_cached_body(std::shared_ptr<const std::string> body) {
    cached_body_ = std::move(body);
    if (cached_body_) {
        set_header("Content-Length", std::to_string(cached_body_->size()));
    }
    return *this;
}

HttpResponse& HttpResponse::set_file(const std::string& path, size_t size) {
    file_path_ = path;
    file_size_ = size;
    set_header("Content-Length", std::to_string(file_size_));
    return *this;
}

std::string HttpResponse::serialize_headers() const {
    std::ostringstream ss;
    ss << version_ << " " << status_code_ << " " << status_text_ << "\r\n";
    for (const auto& [key, val] : headers_) {
        ss << key << ": " << val << "\r\n";
    }
    ss << "\r\n";
    return ss.str();
}

std::string HttpResponse::serialize() const {
    if (is_file() || cached_body_) {
        return serialize_headers(); // body is served via sendfile or directly from cached_body_
    }
    return serialize_headers() + body_;
}

HttpResponse HttpResponse::make_200(const std::string& body, const std::string& content_type) {
    HttpResponse res(200, "OK");
    res.set_header("Content-Type", content_type);
    res.set_header("Server", "cpp-http-server/0.3");
    res.set_body(body);
    return res;
}

HttpResponse HttpResponse::make_304() {
    HttpResponse res(304, "Not Modified");
    res.set_header("Server", "cpp-http-server/0.3");
    return res;
}

HttpResponse HttpResponse::make_400(const std::string& msg) {
    HttpResponse res(400, "Bad Request");
    res.set_header("Content-Type", "text/plain");
    res.set_header("Server", "cpp-http-server/0.3");
    res.set_body(msg);
    return res;
}

HttpResponse HttpResponse::make_403(const std::string& msg) {
    HttpResponse res(403, "Forbidden");
    res.set_header("Content-Type", "text/plain");
    res.set_header("Server", "cpp-http-server/0.3");
    res.set_body(msg);
    return res;
}

HttpResponse HttpResponse::resolve_error(int status_code) {
    std::string text;
    switch (status_code) {
        case 400: text = "Bad Request"; break;
        case 403: text = "Forbidden"; break;
        case 404: text = "Not Found"; break;
        case 500: default: text = "Internal Server Error"; status_code = 500; break;
    }
    HttpResponse res(status_code, text);
    res.set_header("Content-Type", "text/html");
    res.set_header("Server", "cpp-http-server/0.3");
    
    std::string body = "<html><head><title>" + std::to_string(status_code) + " " + text + "</title></head>"
                       "<body style=\"font-family:sans-serif; background-color:#0f0f0f; color:#ff4f4f; text-align:center; padding-top:10%;\">"
                       "<h1>Error " + std::to_string(status_code) + "</h1>"
                       "<p>" + text + "</p>"
                       "</body></html>";
    res.set_body(body);
    return res;
}
