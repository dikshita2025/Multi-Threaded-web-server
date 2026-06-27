#include "handlers/FileHandler.h"
#include "handlers/MimeTypes.h"
#include "server/Metrics.h"
#include <sys/stat.h>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <ctime>

#include "concurrency/ThreadPool.h"

// Forward declare ThreadPool global to get pool stats in /metrics endpoint
extern ThreadPool* g_pool;

extern ThreadPool* g_pool;

FileHandler::FileHandler(std::string www_root)
    : www_root_(std::move(www_root)), cache_(1024) { // 1024 cached items max
    if (!www_root_.empty() && www_root_.back() == '/') {
        www_root_.pop_back();
    }
}

bool FileHandler::is_safe_path(const std::string& path) const {
    if (path.find("/../") != std::string::npos ||
        path.rfind("../", 0) == 0 ||
        (path.size() >= 3 && path.substr(path.size() - 3) == "/..") ||
        path == "..") {
        return false;
    }
    return true;
}

std::string FileHandler::format_http_date(time_t time) {
    char buf[128];
    struct tm tm_info;
#ifdef _WIN32
    gmtime_s(&tm_info, &time);
#else
    gmtime_r(&time, &tm_info);
#endif
    strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &tm_info);
    return buf;
}

HttpResponse FileHandler::handle(const HttpRequest& req) {
    // 1. Only allow GET and HEAD requests
    if (req.method() != "GET" && req.method() != "HEAD") {
        Metrics::get_instance().increment_errors();
        return HttpResponse::resolve_error(400);
    }

    // 2. Prevent path traversal attack
    if (!is_safe_path(req.path())) {
        std::cerr << "[FileHandler] Blocked path traversal attempt: " << req.path() << "\n";
        Metrics::get_instance().increment_errors();
        return HttpResponse::resolve_error(403);
    }

    // 3. Intercept `/metrics` JSON endpoint
    if (req.path() == "/metrics") {
        size_t active = 0, total = 0;
        if (g_pool) {
            active = g_pool->active_tasks();
            total = g_pool->thread_count();
        }
        std::string json = Metrics::get_instance().to_json(active, total);
        HttpResponse res = HttpResponse::make_200(json, "application/json");
        Metrics::get_instance().add_bytes_sent(json.size());
        return res;
    }

    // 4. Resolve virtual host root
    std::string root = www_root_;
    std::string host_header = req.get_header("Host");
    size_t colon = host_header.find(':');
    if (colon != std::string::npos) {
        host_header = host_header.substr(0, colon);
    }
    if (!host_header.empty()) {
        std::string vhost_dir = www_root_ + "/hosts/" + host_header;
        struct stat host_st;
        if (::stat(vhost_dir.c_str(), &host_st) == 0 && S_ISDIR(host_st.st_mode)) {
            root = vhost_dir;
        }
    }

    // 5. Resolve path to physical path
    std::string path = req.path();
    if (path.empty() || path == "/") {
        path = "/index.html";
    } else if (path.back() == '/') {
        path += "index.html";
    }

    std::string physical_path = root + path;

    // 6. Stat the file
    struct stat st;
    if (::stat(physical_path.c_str(), &st) != 0) {
        Metrics::get_instance().increment_errors();
        return HttpResponse::resolve_error(404);
    }

    if (S_ISDIR(st.st_mode)) {
        Metrics::get_instance().increment_errors();
        return HttpResponse::resolve_error(404);
    }

    // 7. Caching check: If-Modified-Since
    std::string last_modified = format_http_date(st.st_mtime);
    std::string if_modified_since = req.get_header("If-Modified-Since");

    if (!if_modified_since.empty() && if_modified_since == last_modified) {
        Metrics::get_instance().increment_cache_hits();
        return HttpResponse::make_304();
    }

    // 8. Build the file response
    HttpResponse res(200, "OK");
    res.set_header("Content-Type", MimeTypes::get_type(physical_path));
    res.set_header("Last-Modified", last_modified);
    res.set_header("Server", "cpp-http-server/0.4");

    if (req.keep_alive()) {
        res.set_header("Connection", "keep-alive");
    } else {
        res.set_header("Connection", "close");
    }

    std::string cache_key = physical_path + ":" + std::to_string(st.st_mtime);
    auto cached = cache_.get(cache_key);

    if (cached) {
        Metrics::get_instance().increment_cache_hits();
        res.set_cached_body(cached);
    } else {
        // Cache files smaller than 5MB
        if (st.st_size < 5 * 1024 * 1024) {
            std::ifstream ifs(physical_path, std::ios::binary);
            if (ifs) {
                auto content = std::make_shared<std::string>(
                    std::istreambuf_iterator<char>(ifs),
                    std::istreambuf_iterator<char>());
                cache_.put(cache_key, content);
                res.set_cached_body(content);
            } else {
                res.set_file(physical_path, static_cast<size_t>(st.st_size));
                Metrics::get_instance().add_bytes_sent(st.st_size);
            }
        } else {
            res.set_file(physical_path, static_cast<size_t>(st.st_size));
            Metrics::get_instance().add_bytes_sent(st.st_size);
        }
    }

    return res;
}
