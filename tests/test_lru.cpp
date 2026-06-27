#include "cache/LruCache.h"
#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include <thread>

void test_basic_lru() {
    LruCache<std::string, std::string> cache(2);

    cache.put("key1", std::make_shared<std::string>("val1"));
    cache.put("key2", std::make_shared<std::string>("val2"));

    assert(*cache.get("key1") == "val1");
    assert(*cache.get("key2") == "val2");

    // Evicts key1
    cache.put("key3", std::make_shared<std::string>("val3"));

    assert(cache.get("key1") == nullptr); // evicted
    assert(*cache.get("key2") == "val2");
    assert(*cache.get("key3") == "val3");

    std::cout << "test_basic_lru passed\n";
}

void test_lru_update() {
    LruCache<std::string, std::string> cache(2);

    cache.put("key1", std::make_shared<std::string>("val1"));
    cache.put("key2", std::make_shared<std::string>("val2"));

    // Update key1, should promote it to front
    cache.put("key1", std::make_shared<std::string>("val1-updated"));

    // key2 is now the oldest, so key3 should evict key2, not key1!
    cache.put("key3", std::make_shared<std::string>("val3"));

    assert(*cache.get("key1") == "val1-updated");
    assert(cache.get("key2") == nullptr); // evicted
    assert(*cache.get("key3") == "val3");

    std::cout << "test_lru_update passed\n";
}

void test_concurrent_lru() {
    LruCache<int, std::string> cache(50);
    std::vector<std::thread> threads;

    // Concurrent writers
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([&cache, i]() {
            for (int j = 0; j < 100; ++j) {
                int key = i * 100 + j;
                cache.put(key, std::make_shared<std::string>("value_" + std::to_string(key)));
            }
        });
    }

    // Concurrent readers
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([&cache]() {
            for (int j = 0; j < 1000; ++j) {
                cache.get(j % 50); // read hot keys
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    std::cout << "test_concurrent_lru passed\n";
}

int main() {
    std::cout << "Running LruCache tests...\n";
    test_basic_lru();
    test_lru_update();
    test_concurrent_lru();
    std::cout << "All LruCache tests passed successfully!\n";
    return 0;
}
