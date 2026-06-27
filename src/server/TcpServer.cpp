#include "server/TcpServer.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <iostream>

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────

TcpServer::TcpServer(uint16_t port, int backlog)
    : port_(port), backlog_(backlog)
{
    init();
}

TcpServer::~TcpServer()
{
    stop();  // signal the accept loop to exit
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// init()
//
// Creates and configures the listening socket.
//
// Key decisions:
//   SO_REUSEADDR  – lets us rebind the port immediately after restart
//                   without waiting for TIME_WAIT to expire (~60s).
//   SO_REUSEPORT  – (commented out) would allow multiple processes to
//                   share the same port (Nginx worker model) — Phase 5.
//   AF_INET       – IPv4. Add AF_INET6 + IPV6_V6ONLY=0 for dual-stack.
// ─────────────────────────────────────────────────────────────────────────────
void TcpServer::init()
{
    // 1. Create TCP socket
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        throw std::runtime_error(
            std::string("socket() failed: ") + std::strerror(errno));
    }

    // 2. SO_REUSEADDR – avoid "Address already in use" on restart
    int opt = 1;
    if (::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        ::close(listen_fd_);
        throw std::runtime_error(
            std::string("setsockopt(SO_REUSEADDR) failed: ") + std::strerror(errno));
    }

    // 3. SO_REUSEPORT – allows multiple processes to share the same port
    //    (useful for multi-process load balancing, e.g. Nginx worker model)
#ifdef SO_REUSEPORT
    if (::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
        std::cerr << "[TcpServer] setsockopt(SO_REUSEPORT) unavailable: " << std::strerror(errno) << "\n";
    }
#endif

    // 3. Bind to 0.0.0.0:<port>
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;     // bind all interfaces
    addr.sin_port        = htons(port_);   // host-to-network byte order

    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(listen_fd_);
        throw std::runtime_error(
            std::string("bind() failed on port ") + std::to_string(port_)
            + ": " + std::strerror(errno));
    }

    // 4. Start listening
    //    backlog_ = SOMAXCONN (128-4096 depending on OS) controls the
    //    kernel's SYN + ESTABLISHED queue depth before accept() drains it.
    if (::listen(listen_fd_, backlog_) < 0) {
        ::close(listen_fd_);
        throw std::runtime_error(
            std::string("listen() failed: ") + std::strerror(errno));
    }

    std::cout << "[TcpServer] Listening on 0.0.0.0:" << port_
              << "  (backlog=" << backlog_ << ")\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// run()
//
// The main accept loop. Blocks until stop() is called.
//
// For each accepted connection:
//   1. Log the client IP + port
//   2. Call the user-supplied handler (synchronous in Phase 1)
//   3. The handler owns the fd and must close it
//
// Phase 2 upgrade: replace direct handler() call with pool.enqueue(...)
// ─────────────────────────────────────────────────────────────────────────────
void TcpServer::run(ClientHandler handler)
{
    running_.store(true, std::memory_order_release);
    std::cout << "[TcpServer] Server started. Press Ctrl+C to stop.\n";

    while (running_.load(std::memory_order_acquire)) {
        sockaddr_in client_addr{};
        socklen_t   client_len = sizeof(client_addr);

        // accept() blocks until a client connects OR we're interrupted.
        // EINTR = interrupted by a signal (e.g. SIGINT) — not a real error.
        int client_fd = ::accept(listen_fd_,
                                 reinterpret_cast<sockaddr*>(&client_addr),
                                 &client_len);
        if (client_fd < 0) {
            if (!running_.load(std::memory_order_acquire)) break;  // intentional stop
            if (errno == EINTR) continue;                          // signal, retry
            std::cerr << "[TcpServer] accept() error: " << std::strerror(errno) << "\n";
            continue;
        }

        // Log the new connection
        char client_ip[INET_ADDRSTRLEN];
        ::inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        std::cout << "[TcpServer] Connection from "
                  << client_ip << ":" << ntohs(client_addr.sin_port)
                  << "  fd=" << client_fd << "\n";

        // TCP_NODELAY – disable Nagle's algorithm on the client socket.
        // Nagle buffers small packets together to reduce overhead; for an
        // HTTP server sending headers + body in separate writes this causes
        // an extra ~200ms delay. We want sub-ms response times, so we turn it off.
        int flag = 1;
        ::setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

        // Dispatch to handler (Phase 1: synchronous; Phase 2: thread pool)
        handler(client_fd);
    }

    std::cout << "[TcpServer] Stopped.\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// stop()
//
// Thread-safe and async-signal-safe.
// Sets the running flag to false, then closes the listen socket to unblock
// accept() (which would otherwise block forever).
// ─────────────────────────────────────────────────────────────────────────────
void TcpServer::stop()
{
    bool expected = true;
    if (running_.compare_exchange_strong(expected, false,
                                         std::memory_order_acq_rel)) {
        if (listen_fd_ >= 0) {
            ::shutdown(listen_fd_, SHUT_RDWR);
            ::close(listen_fd_);
            listen_fd_ = -1;
        }
    }
}
