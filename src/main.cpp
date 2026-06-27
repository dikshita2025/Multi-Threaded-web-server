#include "server/TcpServer.h"
#include "concurrency/ThreadPool.h"
#ifdef __APPLE__
#include "server/KqueueServer.h"
#endif

#include <unistd.h>
#include <csignal>
#include <iostream>
#include <sstream>
#include <string>
#include <cstring>
#include <ctime>
#include <thread>

#include "http/HttpParser.h"
#include "http/HttpRequest.h"
#include "http/HttpResponse.h"
#include "handlers/FileHandler.h"
#include "server/Metrics.h"
#include "logging/Logger.h"
#include <chrono>

#include <fcntl.h>
#ifdef __APPLE__
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#else
#include <sys/sendfile.h>
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Global pointers for signal handler (must be async-signal-safe).
static TcpServer*    g_server        = nullptr;
ThreadPool*          g_pool          = nullptr; // non-static, accessed extern in FileHandler.cpp
FileHandler*         g_file_handler  = nullptr; // non-static
#ifdef __APPLE__
static KqueueServer* g_kqueue_server = nullptr;
#endif

static void signal_handler(int signum)
{
    std::cout << "\n[main] Caught signal " << signum << ", shutting down...\n";
#ifdef __APPLE__
    if (g_kqueue_server) g_kqueue_server->stop();
#endif
    if (g_server) g_server->stop();
    Logger::get_instance().shutdown();
}

// ─────────────────────────────────────────────────────────────────────────────
// timestamp()  –  returns "[HH:MM:SS]" string for logging
// ─────────────────────────────────────────────────────────────────────────────
static std::string timestamp()
{
    std::time_t now = std::time(nullptr);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&now));
    return std::string("[") + buf + "]";
}

// ─────────────────────────────────────────────────────────────────────────────
// handle_client()
//
// Reads data streaming from the socket, parses it statefully, routes it to
// FileHandler, and sends back the headers and zero-copy files if applicable.
// Supports keep-alive connections.
// ─────────────────────────────────────────────────────────────────────────────
static void handle_client(int client_fd)
{
    HttpParser parser;
    char buf[4096];

    while (true) {
        HttpRequest req;
        parser.reset();
        auto start_time = std::chrono::steady_clock::now();

        // ── Read and Parse single request ────────────────────────────────────
        while (true) {
            ssize_t n = ::recv(client_fd, buf, sizeof(buf), 0);
            if (n < 0) {
                if (errno == EINTR) continue;
                ::close(client_fd);
                return;
            } else if (n == 0) {
                ::close(client_fd);
                return;
            }

            auto status = parser.parse(req, buf, static_cast<size_t>(n));
            if (status == HttpParser::ParseStatus::COMPLETE) {
                Metrics::get_instance().increment_requests();
                break;
            } else if (status == HttpParser::ParseStatus::ERROR) {
                Metrics::get_instance().increment_requests();
                Metrics::get_instance().increment_errors();
                HttpResponse res = HttpResponse::resolve_error(400);
                std::string raw_res = res.serialize();
                ::send(client_fd, raw_res.data(), raw_res.size(), 0);
                ::close(client_fd);
                return;
            }
        }

        HttpResponse res = g_file_handler->handle(req);
        bool should_keep_alive = req.keep_alive() && (res.status_code() < 400);

        std::string response_str = res.serialize();
        ssize_t sent = 0;
        const char* data = response_str.data();
        size_t remaining = response_str.size();

        bool send_failed = false;
        while (remaining > 0) {
#ifdef __APPLE__
            ssize_t w = ::send(client_fd, data + sent, remaining, 0);
#else
            ssize_t w = ::send(client_fd, data + sent, remaining, MSG_NOSIGNAL);
#endif
            if (w <= 0) {
                send_failed = true;
                break;
            }
            sent      += w;
            remaining -= static_cast<size_t>(w);
            Metrics::get_instance().add_bytes_sent(static_cast<size_t>(w));
        }

        if (send_failed) {
            ::close(client_fd);
            return;
        }

        // ── Send cached body if present ──────────────────────────────────────
        if (res.cached_body()) {
            const char* cdata = res.cached_body()->data();
            size_t cremaining = res.cached_body()->size();
            while (cremaining > 0) {
#ifdef __APPLE__
                ssize_t w = ::send(client_fd, cdata, cremaining, 0);
#else
                ssize_t w = ::send(client_fd, cdata, cremaining, MSG_NOSIGNAL);
#endif
                if (w <= 0) {
                    send_failed = true;
                    break;
                }
                cdata      += w;
                cremaining -= static_cast<size_t>(w);
                Metrics::get_instance().add_bytes_sent(static_cast<size_t>(w));
            }
        }

        if (send_failed) {
            ::close(client_fd);
            return;
        }

        // ── Send File zero-copy using sendfile() syscall ─────────────────────
        if (res.is_file()) {
            int file_fd = ::open(res.file_path().c_str(), O_RDONLY);
            if (file_fd >= 0) {
                off_t offset = 0;
#ifdef __APPLE__
                off_t len = res.file_size();
                int sf_res = ::sendfile(file_fd, client_fd, offset, &len, nullptr, 0);
                (void)sf_res;
#else
                ssize_t sf_res = ::sendfile(client_fd, file_fd, &offset, res.file_size());
                (void)sf_res;
#endif
                ::close(file_fd);
            } else {
                std::cerr << "[main] Failed to open file for sending: " << res.file_path() << "\n";
            }
        }

        // ── Logging ──────────────────────────────────────────────────────────
        auto end_time = std::chrono::steady_clock::now();
        auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
        
        size_t bytes = res.body().size();
        if (res.is_file()) bytes = res.file_size();
        else if (res.cached_body()) bytes = res.cached_body()->size();
        
        std::ostringstream log_ss;
        log_ss << timestamp() << " " << req.method() << " " << req.path() << " "
               << res.status_code() << " " << bytes << " " << latency << "ms";
        Logger::get_instance().log(log_ss.str());

        if (!should_keep_alive) {
            break;
        }
    }

    ::close(client_fd);
}

// ─────────────────────────────────────────────────────────────────────────────
// main()
// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char* argv[])
{
    uint16_t    port       = 8080;
    std::size_t num_threads = std::thread::hardware_concurrency();

    if (argc > 1) {
        try {
            int p = std::stoi(argv[1]);
            if (p < 1 || p > 65535) throw std::out_of_range("port out of range");
            port = static_cast<uint16_t>(p);
        } catch (const std::exception& e) {
            std::cerr << "Invalid port: " << argv[1] << "\n";
            return 1;
        }
    }
    if (argc > 2) {
        try {
            int t = std::stoi(argv[2]);
            if (t < 1 || t > 256) throw std::out_of_range("threads out of range");
            num_threads = static_cast<std::size_t>(t);
        } catch (const std::exception& e) {
            std::cerr << "Invalid thread count: " << argv[2] << "\n";
            return 1;
        }
    }

    std::cout << "╔══════════════════════════════════════╗\n";
    std::cout << "║  C++ HTTP Server — Phase 6 (kqueue)  ║\n";
    std::cout << "╚══════════════════════════════════════╝\n";

    try {
        // ── Initialize Logger ────────────────────────────────────────────────
        Logger::get_instance().init("server.log");

        // ── Create File Handler ──────────────────────────────────────────────
        FileHandler file_handler("./www");
        g_file_handler = &file_handler;

        // ── Create thread pool FIRST (before server starts accepting) ────────
        ThreadPool pool(num_threads);
        g_pool = &pool;

#ifdef __APPLE__
        // ── macOS: use kqueue-based non-blocking I/O multiplexer ──────────────
        KqueueServer server(port);
        g_kqueue_server = &server;
#else
        // ── Linux: fallback to blocking TcpServer ─────────────────────────────
        TcpServer server(port);
        g_server = &server;
#endif

        std::signal(SIGINT,  signal_handler);
        std::signal(SIGTERM, signal_handler);
#ifdef __APPLE__
        std::signal(SIGPIPE, SIG_IGN);
#endif

        std::cout << "[main] I/O model : "
#ifdef __APPLE__
            "kqueue (non-blocking, edge-triggered)\n"
#else
            "blocking accept + thread pool\n"
#endif
            ;
        std::cout << "[main] Workers   : " << pool.thread_count() << " threads\n";
        std::cout << "[main] Usage     : ./http_server [port] [threads]\n";
        std::cout << "[main] Open      : http://localhost:" << port << "\n";
        std::cout << "[main] Bench     : ab -n 10000 -c 100 http://127.0.0.1:" << port << "/\n\n";

        // ── Event / Accept loop ───────────────────────────────────────────────
        server.run([&pool](int client_fd) {
            pool.enqueue([client_fd] {
                handle_client(client_fd);  // runs on a worker thread
            });
        });

    } catch (const std::exception& e) {
        std::cerr << "[main] Fatal error: " << e.what() << "\n";
        g_file_handler = nullptr;
        return 1;
    }

    g_file_handler = nullptr;
    std::cout << "[main] Clean exit.\n";
    return 0;
}
