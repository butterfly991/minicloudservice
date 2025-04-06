#pragma once

#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <functional>
#include <chrono>

namespace cloud {
namespace sync {

// RAII wrapper for mutex
template<typename Mutex>
class ScopedLock {
    Mutex& mutex_;
public:
    explicit ScopedLock(Mutex& mutex) : mutex_(mutex) {
        mutex_.lock();
    }
    ~ScopedLock() {
        mutex_.unlock();
    }
    ScopedLock(const ScopedLock&) = delete;
    ScopedLock& operator=(const ScopedLock&) = delete;
};

// RAII wrapper for shared mutex
template<typename SharedMutex>
class ScopedSharedLock {
    SharedMutex& mutex_;
public:
    explicit ScopedSharedLock(SharedMutex& mutex) : mutex_(mutex) {
        mutex_.lock_shared();
    }
    ~ScopedSharedLock() {
        mutex_.unlock_shared();
    }
    ScopedSharedLock(const ScopedSharedLock&) = delete;
    ScopedSharedLock& operator=(const ScopedSharedLock&) = delete;
};

// Thread-safe singleton wrapper
template<typename T>
class ThreadSafeSingleton {
    static std::unique_ptr<T> instance_;
    static std::mutex mutex_;
    static std::atomic<bool> initialized_;

public:
    static T& get_instance() {
        if (!initialized_.load(std::memory_order_acquire)) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!initialized_.load(std::memory_order_relaxed)) {
                instance_ = std::make_unique<T>();
                initialized_.store(true, std::memory_order_release);
            }
        }
        return *instance_;
    }

    template<typename... Args>
    static void initialize(Args&&... args) {
        if (!initialized_.load(std::memory_order_acquire)) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!initialized_.load(std::memory_order_relaxed)) {
                instance_ = std::make_unique<T>(std::forward<Args>(args)...);
                initialized_.store(true, std::memory_order_release);
            }
        }
    }
};

template<typename T>
std::unique_ptr<T> ThreadSafeSingleton<T>::instance_;

template<typename T>
std::mutex ThreadSafeSingleton<T>::mutex_;

template<typename T>
std::atomic<bool> ThreadSafeSingleton<T>::initialized_{false};

// Thread-safe queue with condition variable
template<typename T>
class ThreadSafeQueue {
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable not_empty_;
    std::atomic<bool> stopped_{false};

public:
    void push(T value) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(std::move(value));
        }
        not_empty_.notify_one();
    }

    bool pop(T& value) {
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_.wait(lock, [this] {
            return !queue_.empty() || stopped_.load(std::memory_order_acquire);
        });

        if (queue_.empty() && stopped_.load(std::memory_order_acquire)) {
            return false;
        }

        value = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    void stop() {
        stopped_.store(true, std::memory_order_release);
        not_empty_.notify_all();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }
};

// Thread-safe circular buffer
template<typename T, size_t Size>
class ThreadSafeCircularBuffer {
    std::array<T, Size> buffer_;
    std::atomic<size_t> head_{0};
    std::atomic<size_t> tail_{0};
    std::atomic<size_t> count_{0};
    mutable std::mutex mutex_;
    std::condition_variable not_full_;
    std::condition_variable not_empty_;

public:
    bool push(T value) {
        std::unique_lock<std::mutex> lock(mutex_);
        not_full_.wait(lock, [this] { return count_.load(std::memory_order_acquire) < Size; });

        buffer_[head_.load(std::memory_order_relaxed)] = std::move(value);
        head_.store((head_.load(std::memory_order_relaxed) + 1) % Size, std::memory_order_release);
        count_.fetch_add(1, std::memory_order_release);

        not_empty_.notify_one();
        return true;
    }

    bool pop(T& value) {
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_.wait(lock, [this] { return count_.load(std::memory_order_acquire) > 0; });

        value = std::move(buffer_[tail_.load(std::memory_order_relaxed)]);
        tail_.store((tail_.load(std::memory_order_relaxed) + 1) % Size, std::memory_order_release);
        count_.fetch_sub(1, std::memory_order_release);

        not_full_.notify_one();
        return true;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return count_.load(std::memory_order_acquire) == 0;
    }

    bool full() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return count_.load(std::memory_order_acquire) == Size;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return count_.load(std::memory_order_acquire);
    }
};

// Thread-safe cache with LRU eviction
template<typename K, typename V, size_t Capacity>
class ThreadSafeLRUCache {
    struct Node {
        K key;
        V value;
        std::chrono::steady_clock::time_point last_access;
    };

    std::unordered_map<K, typename std::list<Node>::iterator> map_;
    std::list<Node> list_;
    mutable std::shared_mutex mutex_;
    std::condition_variable not_full_;

public:
    bool put(const K& key, V value) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = map_.find(key);
        if (it != map_.end()) {
            it->second->value = std::move(value);
            it->second->last_access = std::chrono::steady_clock::now();
            list_.splice(list_.begin(), list_, it->second);
            return true;
        }

        if (map_.size() >= Capacity) {
            auto oldest = list_.back();
            map_.erase(oldest.key);
            list_.pop_back();
        }

        list_.push_front({key, std::move(value), std::chrono::steady_clock::now()});
        map_[key] = list_.begin();
        return true;
    }

    std::optional<V> get(const K& key) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = map_.find(key);
        if (it == map_.end()) {
            return std::nullopt;
        }

        it->second->last_access = std::chrono::steady_clock::now();
        list_.splice(list_.begin(), list_, it->second);
        return it->second->value;
    }

    bool remove(const K& key) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = map_.find(key);
        if (it == map_.end()) {
            return false;
        }

        list_.erase(it->second);
        map_.erase(it);
        return true;
    }

    size_t size() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return map_.size();
    }

    bool empty() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return map_.empty();
    }
};

} // namespace sync
} // namespace cloud 