#pragma once

#include <atomic>
#include <cstddef>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// Metrics
//
// A simple thread-safe global structure to collect server operational metrics.
// Thread-safety is achieved using atomic variables (lock-free).
// ─────────────────────────────────────────────────────────────────────────────
class Metrics {
public:
    static Metrics& get_instance() {
        static Metrics instance;
        return instance;
    }

    void increment_requests() { total_requests_.fetch_add(1, std::memory_order_relaxed); }
    void increment_cache_hits() { cache_hits_.fetch_add(1, std::memory_order_relaxed); }
    void increment_errors() { errors_.fetch_add(1, std::memory_order_relaxed); }
    void add_bytes_sent(size_t bytes) { bytes_sent_.fetch_add(bytes, std::memory_order_relaxed); }

    size_t total_requests() const { return total_requests_.load(std::memory_order_relaxed); }
    size_t cache_hits() const { return cache_hits_.load(std::memory_order_relaxed); }
    size_t errors() const { return errors_.load(std::memory_order_relaxed); }
    size_t bytes_sent() const { return bytes_sent_.load(std::memory_order_relaxed); }

    std::string to_json(size_t active_threads, size_t pool_threads) const {
        return "{\n"
               "  \"total_requests\": " + std::to_string(total_requests()) + ",\n"
               "  \"cache_hits\": " + std::to_string(cache_hits()) + ",\n"
               "  \"errors\": " + std::to_string(errors()) + ",\n"
               "  \"bytes_sent\": " + std::to_string(bytes_sent()) + ",\n"
               "  \"active_worker_threads\": " + std::to_string(active_threads) + ",\n"
               "  \"total_worker_threads\": " + std::to_string(pool_threads) + "\n"
               "}";
    }

private:
    Metrics() = default;

    std::atomic<size_t> total_requests_{ 0 };
    std::atomic<size_t> cache_hits_{ 0 };
    std::atomic<size_t> errors_{ 0 };
    std::atomic<size_t> bytes_sent_{ 0 };
};
