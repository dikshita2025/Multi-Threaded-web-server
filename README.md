# C++ HTTP Server — From Scratch

[![Build](https://img.shields.io/badge/build-CMake-blue)](CMakeLists.txt)
[![Language](https://img.shields.io/badge/C%2B%2B-17-brightgreen)]()
[![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Linux-lightgrey)]()
[![Throughput](https://img.shields.io/badge/throughput-~30k_req%2Fsec-orange)]()

> A **high-performance, multi-threaded HTTP/1.1 web server** built from scratch in C++ using POSIX sockets, a hand-rolled thread pool, kqueue I/O multiplexing, LRU cache, and async logging. No frameworks. No shortcuts.

---

## Architecture

```
Client ──TCP──▶ [kqueue Event Loop]       ← O_NONBLOCK, edge-triggered
                      │
                      ▼
                [Thread Pool]              ← std::thread + condition_variable
                 ┌───┴─────────┐
                 ▼             ▼
          [HTTP Parser]  [LRU Cache]       ← O(1) get/put, shared_mutex
                 │
                 ▼
          [File Handler]                   ← sendfile() zero-copy (>5MB)
          [Cached Body]                    ← shared_ptr<string> in-RAM (<5MB)
                 │
                 ▼
          [Async Logger]                   ← queue swap, background writer
```

---

## Phases

| Phase | Feature | Status |
|-------|---------|--------|
| 1 | TCP Socket Server (POSIX, RAII, signal-safe) | ✅ |
| 2 | Thread Pool (mutex + condition_variable) | ✅ |
| 3 | HTTP/1.1 Parser (RFC 7230, keep-alive, %xx decoding) | ✅ |
| 4 | Static File Server + `sendfile()` zero-copy + virtual hosts | ✅ |
| 5 | LRU Cache (O(1), readers-writer lock) + Async Logger | ✅ |
| 6 | kqueue I/O multiplexing + TCP_NODELAY + Benchmarks | ✅ |

---

## Quick Start

### Prerequisites
- macOS (Clang 13+) or Linux (GCC 10+)
- CMake 3.16+
- `wrk` for sustained benchmarking: `brew install wrk` *(optional)*

### Build & Run

```bash
cd http-server

# Configure + build (Release = O3 + march=native)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Run (port 8080, auto-detects CPU core count for thread pool)
./build/http_server 8080

# Or specify threads explicitly:
./build/http_server 8080 8
```

### Test it
```bash
# Browser
open http://localhost:8080

# curl (verbose — see raw HTTP/1.1 headers)
curl -v http://localhost:8080

# curl JSON metrics endpoint
curl http://localhost:8080/metrics

# ApacheBench — 10,000 requests, 100 concurrent
ab -n 10000 -c 100 http://localhost:8080/

# Run full benchmark matrix
./benchmark/run_bench.sh
```

### Run Unit Tests
```bash
./build/test_parser   # HTTP/1.1 parser tests
./build/test_lru      # LRU cache concurrent access tests
```

---

## Benchmark Results (Phase 6)

Measured on macOS (Apple Silicon), kqueue I/O, LRU cache warm, 2000 requests:

| Threads | Req/sec | Avg Latency | I/O Model |
|---------|---------|-------------|-----------|
| 1 | **~31,800** | 0.31 ms | kqueue + thread pool |
| 2 | **~26,700** | 0.37 ms | kqueue + thread pool |
| 4 | **~23,800** | 0.42 ms | kqueue + thread pool |
| 8 | **~19,600** | 0.51 ms | kqueue + thread pool |

> Note: 1 thread wins at low concurrency because there's zero lock contention. More threads win under high concurrency (c=100+). Run `./benchmark/run_bench.sh 8090 10000 100` for a fair multi-thread comparison.

**Live metrics** available at `http://localhost:8080/metrics` (JSON):
```json
{
  "total_requests": 20001,
  "cache_hits": 19999,
  "errors": 0,
  "bytes_sent": 88261612,
  "active_worker_threads": 0,
  "total_worker_threads": 8
}
```

---

## Key Design Decisions

### Why `kqueue` / non-blocking I/O?
`accept()` in a blocking loop has one major problem: it can only accept one connection at a time per thread. With `kqueue` (macOS) / `epoll` (Linux), a single thread monitors thousands of sockets using kernel-space event notification. When a connection is ready, we drain all pending connections in a loop (`EAGAIN` signals "all done"), then dispatch to the thread pool. This eliminates the "thundering herd" problem.

### Why `TCP_NODELAY`?
Nagle's algorithm buffers small TCP segments to improve network efficiency. For an HTTP server writing headers and body in separate `send()` calls, this causes a 40–200ms artificial delay per response. `TCP_NODELAY` disables this, giving sub-millisecond response times.

### Why `SO_REUSEADDR` + `SO_REUSEPORT`?
- `SO_REUSEADDR`: After a crash, ports stay in `TIME_WAIT` for ~60s. This lets us rebind immediately.
- `SO_REUSEPORT`: Allows multiple processes to share the same port (Nginx worker model), enabling zero-downtime restarts.

### Why a Thread Pool instead of Thread-per-request?
Thread-per-request: spawning a thread costs ~8MB stack + ~20µs. Under 10k concurrent connections = 80GB RAM just for stacks. A fixed thread pool amortizes this: threads are created once and reused forever.

### LRU Cache: Why `unordered_map + list`?
- `unordered_map<key, list_iterator>` → O(1) lookup by key
- `std::list` (doubly-linked) → O(1) `splice` to move-to-front (LRU promotion)
- Combined: **O(1) get, O(1) put** — the canonical textbook LRU implementation
- Cache key = `path + ":" + mtime` → automatic cache invalidation when files change

### Why `shared_ptr<const string>` in the cache?
Returning a `shared_ptr` to cached file contents means zero copying regardless of how many concurrent threads read the same file. The reference count keeps the memory alive even if the LRU evicts the entry while a response is in-flight.

### Why async logging?
Disk writes are slow (µs–ms range). If the hot path blocks on a log write, it stalls the entire worker thread handling that connection. The async logger enqueues log strings in < 100ns (mutex + queue push), and a dedicated background thread does all the actual disk writes. This keeps the critical path disk-I/O-free.

---

## Interview Topics This Project Covers

| Topic | Where in Code |
|-------|--------------|
| **The C10K problem** | `KqueueServer.cpp` — event loop design |
| **Nagle's algorithm** | `TcpServer.cpp` — `TCP_NODELAY` |
| **Amdahl's Law** | Benchmark results: 1t wins at low concurrency |
| **Cache-aside pattern** | `FileHandler.cpp` — check cache → read disk → populate |
| **Readers-writer lock** | `LruCache.h` — `std::shared_mutex` |
| **HTTP/1.1 spec** | `HttpParser.cpp` — `\r\n`, `Content-Length`, keep-alive |
| **Zero-copy I/O** | `main.cpp` — `sendfile()` vs cached `shared_ptr` |
| **Async-signal-safe** | `main.cpp` — signal handler only sets atomics |
| **RAII** | `TcpServer.cpp` — socket lifetime via constructor/destructor |
| **Edge-triggered I/O** | `KqueueServer.cpp` — `EV_CLEAR` + drain loop |

---

## Project Structure

```
http-server/
├── CMakeLists.txt           ← C++17, AddressSanitizer, Release/Debug
├── README.md
├── src/
│   ├── main.cpp             ← Entry point, signal handling, keep-alive loop
│   ├── server/
│   │   ├── TcpServer.h/.cpp       ← Blocking accept loop (Linux fallback)
│   │   ├── KqueueServer.h/.cpp    ← kqueue event loop (macOS)
│   │   └── Metrics.h              ← Lock-free atomic counters
│   ├── concurrency/
│   │   └── ThreadPool.h/.cpp      ← Fixed thread pool, work-stealing queue
│   ├── http/
│   │   ├── HttpParser.h/.cpp      ← State-machine HTTP/1.1 parser
│   │   ├── HttpRequest.h/.cpp     ← Request model (method, path, headers, body)
│   │   └── HttpResponse.h/.cpp    ← Response builder (headers + body/file/cache)
│   ├── handlers/
│   │   ├── FileHandler.h/.cpp     ← Static files, vhost routing, path security
│   │   └── MimeTypes.h            ← Extension → MIME type registry
│   ├── cache/
│   │   └── LruCache.h             ← Thread-safe O(1) LRU (template)
│   └── logging/
│       └── Logger.h/.cpp          ← Async background-writer logger
├── www/
│   └── index.html           ← Server landing page
├── tests/
│   ├── test_parser.cpp      ← HTTP parser unit tests
│   └── test_lru.cpp         ← LRU cache concurrent tests
└── benchmark/
    └── run_bench.sh         ← ApacheBench matrix + wrk integration
```
