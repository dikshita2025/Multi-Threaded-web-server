# C++ Multi-Threaded HTTP Server

A multi-threaded HTTP/1.1 web server built from scratch in **C++17** using POSIX sockets. The project focuses on systems programming concepts including concurrent request handling, event-driven I/O, HTTP parsing, caching, and efficient file serving.

## Features

* Multi-threaded request handling using a custom thread pool
* HTTP/1.1 request parsing with persistent (keep-alive) connections
* Event-driven networking using `kqueue` (macOS)
* Static file serving
* Thread-safe LRU cache for frequently accessed files
* Zero-copy file transfer using `sendfile()` for large files
* Asynchronous logging
* Built-in metrics endpoint (`/metrics`)

---

## Architecture

```text
               Client
                  │
                  ▼
        kqueue Event Loop
                  │
                  ▼
            Thread Pool
                  │
      ┌───────────┼───────────┐
      ▼           ▼           ▼
 HTTP Parser   File Handler  Logger
                  │
                  ▼
             LRU Cache
```

---

## Technologies Used

* C++17
* POSIX Sockets
* `kqueue`
* CMake
* `std::thread`
* `std::mutex`
* `std::condition_variable`
* `std::shared_mutex`
* `sendfile()`

---

## Project Structure

```text
http-server/
├── src/
│   ├── server/
│   ├── http/
│   ├── handlers/
│   ├── concurrency/
│   ├── cache/
│   └── logging/
├── tests/
├── benchmark/
├── www/
├── CMakeLists.txt
└── README.md
```

---

## Building the Project

```bash
git clone <repository-url>
cd http-server

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Run the server:

```bash
./build/http_server 8080
```

---

## Testing

Open the server in your browser:

```text
http://localhost:8080
```

Using `curl`:

```bash
curl http://localhost:8080
```

Metrics endpoint:

```bash
curl http://localhost:8080/metrics
```

Benchmark using ApacheBench:

```bash
ab -n 10000 -c 100 http://localhost:8080/
```

---

## Performance

The server was benchmarked on an Apple Silicon Mac using ApacheBench.

| Worker Threads | Requests/sec |
| -------------: | -----------: |
|              1 |      ~31,800 |
|              2 |      ~26,700 |
|              4 |      ~23,800 |
|              8 |      ~19,600 |

*Performance varies depending on hardware and workload.*

---

## Learning Outcomes

This project helped me gain practical experience with:

* Socket programming
* HTTP/1.1 protocol
* Multi-threading in C++
* Event-driven I/O using `kqueue`
* Thread synchronization
* LRU cache design
* Zero-copy file transfer
* Performance benchmarking and optimization

---

## Future Improvements

* Linux support using `epoll`
* HTTPS/TLS support
* HTTP/2 support
* Dynamic routing
* Load testing under higher concurrency
