#pragma once

#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <fstream>
#include <atomic>

// ─────────────────────────────────────────────────────────────────────────────
// Logger
//
// An Asynchronous Request Logger.
// Worker threads submit formatted log strings to a thread-safe queue. A dedicated
// background thread drains the queue and writes to `server.log` on disk.
// This prevents disk write latency from blocking HTTP response threads.
// ─────────────────────────────────────────────────────────────────────────────
class Logger {
public:
    static Logger& get_instance() {
        static Logger instance;
        return instance;
    }

    // Not copyable
    Logger(const Logger&)            = delete;
    Logger& operator=(const Logger&) = delete;

    // Initializes the logger and starts the background writer thread.
    // Writes to `log_file_path`.
    void init(const std::string& log_file_path);

    // Stops the background thread and flushes remaining logs to disk.
    void shutdown();

    // Enqueues a log message asynchronously. Returns immediately.
    void log(std::string message);

    ~Logger();

private:
    Logger() = default;

    void writer_loop();

    std::string             file_path_;
    std::ofstream           log_file_;
    std::queue<std::string> queue_;
    
    std::mutex              mutex_;
    std::condition_variable cv_;
    
    std::thread             writer_thread_;
    std::atomic<bool>       stop_{ false };
    std::atomic<bool>       initialized_{ false };
};
