#ifdef __APPLE__

#include "server/KqueueServer.h"
#include <stdexcept>
#include <cerrno>
#include <cstring>
#include <iostream>

// ─────────────────────────────────────────────────────────────────────────────
// set_nonblocking()
//
// Makes a file descriptor non-blocking using F_SETFL / O_NONBLOCK.
// Non-blocking is essential for kqueue — if we accidentally block on
// a read/accept(), the entire event loop stalls.
// ─────────────────────────────────────────────────────────────────────────────
void KqueueServer::set_nonblocking(int fd)
{
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) throw std::runtime_error("fcntl(F_GETFL) failed");
    if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        throw std::runtime_error("fcntl(F_SETFL, O_NONBLOCK) failed");
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────
KqueueServer::KqueueServer(uint16_t port, int backlog)
    : port_(port), backlog_(backlog)
{
    init();
}

KqueueServer::~KqueueServer()
{
    stop();
    if (kqueue_fd_ >= 0) { ::close(kqueue_fd_); kqueue_fd_ = -1; }
    if (listen_fd_ >= 0) { ::close(listen_fd_); listen_fd_ = -1; }
}

// ─────────────────────────────────────────────────────────────────────────────
// init()
//
// Creates the kqueue file descriptor and the listening socket,
// then registers the listening socket with kqueue.
// ─────────────────────────────────────────────────────────────────────────────
void KqueueServer::init()
{
    // 1. Create the kqueue instance
    kqueue_fd_ = ::kqueue();
    if (kqueue_fd_ < 0)
        throw std::runtime_error(std::string("kqueue() failed: ") + std::strerror(errno));

    // 2. Create, configure, bind, and listen on the TCP socket
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0)
        throw std::runtime_error(std::string("socket() failed: ") + std::strerror(errno));

    int opt = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif

    set_nonblocking(listen_fd_);

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port_);

    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(listen_fd_);
        throw std::runtime_error(
            std::string("bind() failed on port ") + std::to_string(port_) + ": " + std::strerror(errno));
    }

    if (::listen(listen_fd_, backlog_) < 0) {
        ::close(listen_fd_);
        throw std::runtime_error(std::string("listen() failed: ") + std::strerror(errno));
    }

    // 3. Register the listen socket with kqueue for read events
    //    EV_ADD   – add this event to the kqueue interest list
    //    EV_CLEAR – after the event fires, clear its "triggered" state
    //               (edge-triggered semantics for accept)
    struct kevent ev;
    EV_SET(&ev, listen_fd_, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, nullptr);
    if (::kevent(kqueue_fd_, &ev, 1, nullptr, 0, nullptr) < 0) {
        throw std::runtime_error(std::string("kevent() registration failed: ") + std::strerror(errno));
    }

    std::cout << "[KqueueServer] kqueue fd=" << kqueue_fd_
              << "  listen fd=" << listen_fd_
              << "  port=" << port_ << "\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// run()
//
// The main kqueue event loop.
//
// How it works:
//   1. Call kevent() to wait for events (blocks until at least one fires).
//   2. For each event:
//      a. If fd == listen_fd_: a new connection is ready — drain all pending
//         connections with accept4() in a loop until EAGAIN (non-blocking).
//         For each new client fd, apply TCP_NODELAY and call the handler.
//      b. Else: a client fd has data — call the handler directly.
//         (In Phase 6 we pass the fd to the ThreadPool.)
//
// Why accept in a loop?
//   kqueue is edge-triggered on EV_CLEAR. If many connections arrive between
//   two kevent() calls, we get ONE event. We must drain fully to avoid
//   missing connections until the next wake.
// ─────────────────────────────────────────────────────────────────────────────
void KqueueServer::run(ClientHandler handler)
{
    running_.store(true, std::memory_order_release);
    std::cout << "[KqueueServer] Event loop started (kqueue). Press Ctrl+C to stop.\n";

    static const int MAX_EVENTS = 1024;
    struct kevent events[MAX_EVENTS];

    while (running_.load(std::memory_order_acquire)) {
        // Block until events arrive (timeout = nullptr → wait forever)
        int n = ::kevent(kqueue_fd_, nullptr, 0, events, MAX_EVENTS, nullptr);

        if (n < 0) {
            if (errno == EINTR) continue;  // signal interrupted us
            if (!running_.load(std::memory_order_acquire)) break;
            std::cerr << "[KqueueServer] kevent() error: " << std::strerror(errno) << "\n";
            continue;
        }

        for (int i = 0; i < n; ++i) {
            const struct kevent& kev = events[i];

            // ── Error on a socket ──────────────────────────────────────────────
            if (kev.flags & EV_ERROR) {
                if (static_cast<int>(kev.ident) != listen_fd_) {
                    ::close(static_cast<int>(kev.ident));
                }
                continue;
            }

            // ── New connection on the listen socket ────────────────────────────
            if (static_cast<int>(kev.ident) == listen_fd_) {
                // Drain all pending connections (edge-triggered semantics)
                while (true) {
                    sockaddr_in client_addr{};
                    socklen_t   client_len = sizeof(client_addr);
                    int client_fd = ::accept(listen_fd_,
                                            reinterpret_cast<sockaddr*>(&client_addr),
                                            &client_len);
                    if (client_fd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break; // all drained
                        if (errno == EINTR) continue;
                        break;
                    }

                    char client_ip[INET_ADDRSTRLEN];
                    ::inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
                    std::cout << "[KqueueServer] Connection from "
                              << client_ip << ":" << ntohs(client_addr.sin_port)
                              << "  fd=" << client_fd << "\n";

                    // TCP_NODELAY: disable Nagle's algorithm for lower latency
                    int flag = 1;
                    ::setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

                    // Dispatch to thread pool (non-blocking in the event loop)
                    handler(client_fd);
                }
            }
        }
    }

    std::cout << "[KqueueServer] Event loop stopped.\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// stop()
// ─────────────────────────────────────────────────────────────────────────────
void KqueueServer::stop()
{
    bool expected = true;
    if (running_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
        // Wake the event loop by closing the kqueue fd.
        // kevent() will then return -1 with EBADF, which we check for.
        if (kqueue_fd_ >= 0) {
            ::close(kqueue_fd_);
            kqueue_fd_ = -1;
        }
        if (listen_fd_ >= 0) {
            ::shutdown(listen_fd_, SHUT_RDWR);
        }
    }
}

#endif // __APPLE__
