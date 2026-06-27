#pragma once

#include <list>
#include <unordered_map>
#include <shared_mutex>
#include <memory>
#include <utility>

// ─────────────────────────────────────────────────────────────────────────────
// LruCache
//
// A thread-safe, templated Least Recently Used (LRU) cache.
// Achieves O(1) get and O(1) put.
//
// Thread Safety:
//   Uses std::shared_mutex (Readers-Writer lock):
//     - get() uses std::shared_lock (multiple readers can read concurrently)
//     - put() uses std::unique_lock (exclusive write access)
//
// Performance / Zero-Copy:
//   Returns std::shared_ptr<const V> to avoid copying large values (like file
//   contents) while ensuring the memory remains valid even if evicted.
// ─────────────────────────────────────────────────────────────────────────────
template <typename K, typename V>
class LruCache {
public:
    explicit LruCache(size_t capacity) : capacity_(capacity) {}

    // Not copyable
    LruCache(const LruCache&)            = delete;
    LruCache& operator=(const LruCache&) = delete;

    // Get an item from the cache. Returns shared_ptr to the value, or nullptr if not found.
    // Promotes the accessed key to the front of the LRU list.
    std::shared_ptr<const V> get(const K& key) {
        // We need an exclusive lock (unique_lock) here because "get" modifies
        // the internal list to move the item to the front (LRU promotion)!
        // If we only used a shared_lock, multiple threads promoting items
        // concurrently would cause list corruption.
        std::unique_lock<std::shared_mutex> lock(mutex_);

        auto it = map_.find(key);
        if (it == map_.end()) {
            return nullptr;
        }

        // Move the accessed item to the front of the list
        list_.splice(list_.begin(), list_, it->second);
        return it->second->second;
    }

    // Insert or update an item in the cache.
    // Evicts the least recently used item if capacity is exceeded.
    void put(const K& key, std::shared_ptr<const V> value) {
        std::unique_lock<std::shared_mutex> lock(mutex_);

        if (capacity_ == 0) return;

        auto it = map_.find(key);
        if (it != map_.end()) {
            // Update value and promote to front
            it->second->second = value;
            list_.splice(list_.begin(), list_, it->second);
            return;
        }

        // If at capacity, evict the least recently used item (from the back)
        if (list_.size() >= capacity_) {
            const K& oldest_key = list_.back().first;
            map_.erase(oldest_key);
            list_.pop_back();
        }

        // Insert new item at the front
        list_.emplace_front(key, std::move(value));
        map_[key] = list_.begin();
    }

    // Clear the cache
    void clear() {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        map_.clear();
        list_.clear();
    }

    size_t size() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return map_.size();
    }

    size_t capacity() const { return capacity_; }

private:
    size_t capacity_;
    // List stores key-value pairs. Using shared_ptr to values for zero-copy safety.
    std::list<std::pair<K, std::shared_ptr<const V>>> list_;
    // Map maps Key to the list iterator for O(1) lookups
    std::unordered_map<K, typename std::list<std::pair<K, std::shared_ptr<const V>>>::iterator> map_;
    mutable std::shared_mutex mutex_;
};
