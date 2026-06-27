#pragma once

#ifdef __APPLE__

#include <sys/event.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <functional>
#include <cstdint>
#include <atomic>
#include <stdexcept>
#include <string>
#include <cstring>
#include <cerrno>
#include <iostream>

// ─────────────────────────────────────────────────────────────────────────────
// KqueueServer
//
// A kqueue-based, non-blocking I/O multiplexed TCP server (macOS only).
//
// Architecture:
//   - A single event loop (on the calling thread) replaces blocking accept().
//   - All sockets (listen + client) are set O_NONBLOCK.
//   - kqueue monitors the listen fd for new connections and each client fd
//     for readable data, eliminating per-connection thread overhead.
//   - When data arrives, the registered ClientHandler is called on the same
//     event-loop thread (Phase 6 uses the ThreadPool to dispatch it).
//
// Interview notes:
//   - kqueue is the BSD/macOS equivalent of Linux's epoll.
//   - EV_SET macro populates a kevent struct: (fd, filter, flags, fflags, data, udata).
//   - EVFILT_READ fires when data is available to read (or a new connection is ready).
//   - KQ_TIMEOUT -1 means "block indefinitely until at least one event fires".
// ─────────────────────────────────────────────────────────────────────────────
class KqueueServer {
public:
    using ClientHandler = std::function<void(int client_fd)>;

    explicit KqueueServer(uint16_t port, int backlog = SOMAXCONN);

    KqueueServer(const KqueueServer&)            = delete;
    KqueueServer& operator=(const KqueueServer&) = delete;
    ~KqueueServer();

    // Blocks until stop() is called.
    // handler is called for each new accepted connection.
    void run(ClientHandler handler);

    // Thread-safe, signal-safe.
    void stop();

    uint16_t port()    const { return port_; }
    bool     running() const { return running_.load(std::memory_order_acquire); }

private:
    static void set_nonblocking(int fd);
    void init();

    uint16_t          port_;
    int               backlog_;
    int               listen_fd_{ -1 };
    int               kqueue_fd_{ -1 };
    std::atomic<bool> running_{ false };
};

#endif // __APPLE__
