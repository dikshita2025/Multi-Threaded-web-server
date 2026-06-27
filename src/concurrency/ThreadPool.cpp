#include "concurrency/ThreadPool.h"

#include <stdexcept>
#include <iostream>

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
//
// Spawns `num_threads` worker threads, each running worker_loop() forever.
// Using hardware_concurrency() as default is a good starting point, but
// for I/O-bound workloads (like an HTTP server) you can exceed it:
//   - CPU-bound: N = hardware_concurrency()
//   - I/O-bound: N = 2x or 4x hardware_concurrency() (threads block on recv/send)
// ─────────────────────────────────────────────────────────────────────────────
ThreadPool::ThreadPool(std::size_t num_threads)
{
    if (num_threads == 0) {
        num_threads = std::thread::hardware_concurrency();
        if (num_threads == 0) num_threads = 4;  // safe fallback
    }

    workers_.reserve(num_threads);

    for (std::size_t i = 0; i < num_threads; ++i) {
        // Each worker is an std::thread running our worker_loop().
        // The thread is immediately started and waits on the condition_variable.
        workers_.emplace_back(&ThreadPool::worker_loop, this);
    }

    std::cout << "[ThreadPool] Started with " << num_threads << " worker thread(s)."
              << " (hardware_concurrency=" << std::thread::hardware_concurrency() << ")\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// Destructor
//
// Graceful shutdown sequence:
//   1. Acquire the lock, set stop_ = true
//   2. notify_all() — wake every sleeping worker
//   3. Release the lock
//   4. join() every worker — waits for in-flight tasks to complete
//
// This ensures no tasks are dropped on shutdown.
// ─────────────────────────────────────────────────────────────────────────────
ThreadPool::~ThreadPool()
{
    {
        std::unique_lock<std::mutex> lock(mutex_);
        stop_ = true;
    }
    // Wake all threads so they can see stop_=true and exit worker_loop()
    cv_.notify_all();

    for (std::thread& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    std::cout << "[ThreadPool] All workers joined. Shutdown complete.\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// enqueue()
//
// Pushes a task onto the work queue and wakes ONE sleeping worker.
// Uses notify_one() (not notify_all()) — only one thread needs to wake
// per task, avoiding the "thundering herd" problem.
//
// Why move(task)?
//   std::function can be expensive to copy (heap allocation for captured
//   state). Moving transfers ownership without copying.
// ─────────────────────────────────────────────────────────────────────────────
void ThreadPool::enqueue(std::function<void()> task)
{
    {
        std::unique_lock<std::mutex> lock(mutex_);

        if (stop_) {
            throw std::runtime_error("[ThreadPool] enqueue() called after shutdown");
        }

        tasks_.push(std::move(task));
    }
    // notify_one() outside the lock — avoids a mutex contention spike where
    // the woken thread immediately tries to reacquire the lock we still hold.
    cv_.notify_one();
}

// ─────────────────────────────────────────────────────────────────────────────
// worker_loop()
//
// Each worker runs this loop forever until stop_=true AND queue is empty.
//
// The critical section (holding mutex_):
//   1. cv_.wait() atomically releases the lock + sleeps
//   2. On wakeup, reacquires the lock + rechecks predicate (handles spurious
//      wakeups — the OS can wake threads without notify())
//   3. Pops the task, releases the lock
//   4. Executes task OUTSIDE the lock → other workers can run in parallel
//
// Why pop before executing?
//   If we held the lock during task execution, ALL other workers would be
//   blocked waiting. The whole point of a thread pool is parallel execution.
// ─────────────────────────────────────────────────────────────────────────────
void ThreadPool::worker_loop()
{
    while (true) {
        std::function<void()> task;

        {
            std::unique_lock<std::mutex> lock(mutex_);

            // Wait until: there's a task OR we're shutting down.
            // The lambda is the predicate — rechecked on every wakeup to guard
            // against spurious wakeups. Equivalent to:
            //   while (tasks_.empty() && !stop_) cv_.wait(lock);
            cv_.wait(lock, [this] {
                return !tasks_.empty() || stop_;
            });

            // If shutting down and no more tasks → exit this worker
            if (stop_ && tasks_.empty()) {
                return;
            }

            // Pop the next task (move out of queue — no copy)
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        // ── Lock is released here ──────────────────────────────────────────
        // Other workers can now dequeue their own tasks concurrently.

        active_tasks_.fetch_add(1, std::memory_order_relaxed);
        try {
            task();   // Execute the task (e.g., handle HTTP client fd)
        } catch (const std::exception& e) {
            // Never let an exception escape a worker — it would call
            // std::terminate() and crash the entire server.
            std::cerr << "[ThreadPool] Worker caught exception: " << e.what() << "\n";
        } catch (...) {
            std::cerr << "[ThreadPool] Worker caught unknown exception.\n";
        }
        active_tasks_.fetch_sub(1, std::memory_order_relaxed);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// queued_tasks()
// ─────────────────────────────────────────────────────────────────────────────
std::size_t ThreadPool::queued_tasks() const
{
    std::unique_lock<std::mutex> lock(mutex_);
    return tasks_.size();
}
