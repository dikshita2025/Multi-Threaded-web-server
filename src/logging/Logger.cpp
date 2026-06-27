#include "logging/Logger.h"
#include <iostream>

void Logger::init(const std::string& log_file_path) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (initialized_) return;

    file_path_ = log_file_path;
    log_file_.open(file_path_, std::ios::out | std::ios::app);
    if (!log_file_.is_open()) {
        std::cerr << "[Logger] Failed to open log file: " << file_path_ << "\n";
        return;
    }

    initialized_ = true;
    stop_ = false;
    writer_thread_ = std::thread(&Logger::writer_loop, this);
    std::cout << "[Logger] Asynchronous logger initialized writing to: " << file_path_ << "\n";
}

void Logger::shutdown() {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!initialized_) return;
        stop_ = true;
    }
    cv_.notify_one();

    if (writer_thread_.joinable()) {
        writer_thread_.join();
    }

    std::unique_lock<std::mutex> lock(mutex_);
    if (log_file_.is_open()) {
        log_file_.close();
    }
    initialized_ = false;
    std::cout << "[Logger] Shutdown complete.\n";
}

void Logger::log(std::string message) {
    if (!initialized_.load(std::memory_order_relaxed)) {
        // Fallback to std::cout if not initialized yet
        std::cout << message << "\n";
        return;
    }

    {
        std::unique_lock<std::mutex> lock(mutex_);
        queue_.push(std::move(message));
    }
    cv_.notify_one();
}

void Logger::writer_loop() {
    while (true) {
        std::queue<std::string> local_queue;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] {
                return !queue_.empty() || stop_;
            });

            if (queue_.empty() && stop_) {
                break;
            }

            // O(1) swap of the queue contents under lock to release the lock immediately
            std::swap(local_queue, queue_);
        }

        // Process file writes outside of the lock
        while (!local_queue.empty()) {
            if (log_file_.is_open()) {
                log_file_ << local_queue.front() << "\n";
            }
            local_queue.pop();
        }
        if (log_file_.is_open()) {
            log_file_.flush();
        }
    }
}

Logger::~Logger() {
    shutdown();
}
