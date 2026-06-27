#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <cstddef>

// ─────────────────────────────────────────────────────────────────────────────
// ThreadPool
//
// A fixed-size thread pool. Workers sleep on a condition_variable until a task
// is enqueued. No dynamic thread spawning at runtime — threads are created once
// in the constructor and destroyed in the destructor.
//
// Design:
//   - tasks_     : bounded FIFO queue of std::function<void()>
//   - mutex_     : protects tasks_ and stop_ flag
//   - cv_        : workers wait here; enqueue() notifies one worker
//   - stop_      : set to true in destructor to drain and exit workers
//
// Thread safety:
//   - enqueue() is safe to call from any thread (including the accept thread)
//   - Destructor waits for all in-flight tasks to finish (graceful drain)
//
// Interview question: "Why condition_variable instead of busy-wait?"
//   → Busy-wait burns CPU even when queue is empty. condition_variable puts
//     the worker thread to sleep in the kernel, consuming 0 CPU while idle.
//
// Interview question: "What is a spurious wakeup?"
//   → The OS can sometimes wake a thread without notify(). We guard against
//     this by rechecking the predicate inside a while loop (done via wait(lk,
//     predicate) which internally loops: while(!pred()) wait(lk)).
// ─────────────────────────────────────────────────────────────────────────────
class ThreadPool {
public:
    // Construct with `num_threads` worker threads.
    // Passing 0 defaults to std::thread::hardware_concurrency().
    explicit ThreadPool(std::size_t num_threads = 0);

    // Not copyable, not movable (owns threads + mutex)
    ThreadPool(const ThreadPool&)            = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&)                 = delete;
    ThreadPool& operator=(ThreadPool&&)      = delete;

    // Graceful shutdown: waits for all queued tasks to finish, then joins threads.
    ~ThreadPool();

    // Submit a task to the pool. Returns immediately (non-blocking).
    // Task will execute on the next available worker thread.
    // Throws std::runtime_error if called after shutdown.
    void enqueue(std::function<void()> task);

    // ── Stats (lock-free reads) ───────────────────────────────────────────────
    std::size_t thread_count()  const { return workers_.size(); }
    std::size_t queued_tasks()  const;  // tasks waiting in queue
    std::size_t active_tasks()  const { return active_tasks_.load(std::memory_order_acquire); }

private:
    void worker_loop();   // the function each worker thread runs forever

    std::vector<std::thread>           workers_;
    std::queue<std::function<void()>>  tasks_;
    mutable std::mutex                 mutex_;
    std::condition_variable            cv_;
    bool                               stop_{ false };
    std::atomic<std::size_t>           active_tasks_{ 0 };
};
