#pragma once

#include <sys/socket.h>
#include <netinet/in.h>
#include <string>
#include <atomic>
#include <functional>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// TcpServer
//
// A blocking, single-threaded TCP server (Phase 1 baseline).
// Handles one client at a time via an accept-loop.
// Designed to be extended in Phase 2 with a ThreadPool.
//
// RAII: The listening socket is closed in the destructor.
// Signal-safe: stop() is async-signal-safe; called from SIGINT handler.
// ─────────────────────────────────────────────────────────────────────────────
class TcpServer {
public:
    // ClientHandler is called for every accepted connection.
    // Receives the connected socket fd; must close it when done.
    using ClientHandler = std::function<void(int client_fd)>;

    // port     – TCP port to listen on (e.g. 8080)
    // backlog  – listen() backlog queue depth (default: SOMAXCONN)
    explicit TcpServer(uint16_t port, int backlog = SOMAXCONN);

    // Not copyable – socket ownership is unique
    TcpServer(const TcpServer&)            = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    // Not movable — std::atomic<bool> is non-movable
    TcpServer(TcpServer&&)            = delete;
    TcpServer& operator=(TcpServer&&) = delete;

    ~TcpServer();

    // Blocks until stop() is called.
    // handler is invoked synchronously (Phase 1) for each client.
    void run(ClientHandler handler);

    // Thread-safe, signal-safe. Breaks out of run().
    void stop();

    // ── Accessors ────────────────────────────────────────────────────────────
    uint16_t    port()     const { return port_; }
    bool        running()  const { return running_.load(std::memory_order_acquire); }
    int         sockfd()   const { return listen_fd_; }

private:
    // Creates, configures, binds, and listens on the TCP socket.
    // Throws std::runtime_error on failure.
    void init();

    uint16_t         port_;
    int              backlog_;
    int              listen_fd_{ -1 };
    std::atomic<bool> running_{ false };
};
