#pragma once

#include "sync_utils.hpp"
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <atomic>
#include <chrono>
#include <type_traits>

namespace cloud {
namespace thread {

class ThreadPool {
public:
    struct ThreadMetrics {
        size_t total_tasks;
        size_t completed_tasks;
        size_t failed_tasks;
        std::chrono::system_clock::time_point last_update;
        std::chrono::milliseconds average_task_time;
    };

    explicit ThreadPool(size_t num_threads = std::thread::hardware_concurrency());
    ~ThreadPool();

    // Prevent copying
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Task submission
    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args) 
        -> std::future<typename std::invoke_result<F, Args...>::type> {
        using return_type = typename std::invoke_result<F, Args...>::type;
        using task_type = std::packaged_task<return_type()>;

        auto task = std::make_shared<task_type>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<return_type> res = task->get_future();
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (stop_) {
                throw std::runtime_error("submit on stopped ThreadPool");
            }
            tasks_.emplace([task]() { (*task)(); });
        }
        condition_.notify_one();
        return res;
    }

    // Thread pool control
    void stop();
    void wait();
    size_t size() const;
    bool is_stopped() const;

    // Управление задачами
    template<typename F, typename... Args>
    auto submitTask(F&& f, Args&&... args) 
        -> std::future<typename std::invoke_result<F, Args...>::type>;

    // Управление пулом
    void start();
    void pause();
    void resume();
    void setThreadCount(size_t count);

    // Мониторинг
    ThreadMetrics getMetrics() const;
    size_t getActiveThreads() const;
    size_t getPendingTasks() const;
    double getUtilization() const;

    // Настройка
    void setMaxQueueSize(size_t size);
    void setTaskTimeout(std::chrono::milliseconds timeout);

private:
    struct Task {
        std::function<void()> func;
        std::chrono::system_clock::time_point created_at;
    };

    std::vector<std::thread> workers_;
    std::queue<Task> tasks_;
    std::mutex queue_mutex_;
    std::condition_variable condition_;
    std::condition_variable pause_condition_;
    
    std::atomic<bool> stop_;
    std::atomic<bool> pause_;
    std::atomic<size_t> active_threads_;
    std::atomic<size_t> max_queue_size_;
    std::atomic<std::chrono::milliseconds> task_timeout_;
    
    ThreadMetrics metrics_;
    mutable std::mutex metrics_mutex_;

    void worker_thread();
    void updateMetrics(const std::string& operation, std::chrono::milliseconds duration);
    bool shouldProcessTask(const Task& task) const;
    void cleanupStaleTasks();
};

// Implementation
inline ThreadPool::ThreadPool(size_t num_threads) {
    for (size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back(&ThreadPool::worker_thread, this);
    }
}

inline ThreadPool::~ThreadPool() {
    stop();
    wait();
}

inline void ThreadPool::stop() {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        stop_ = true;
    }
    condition_.notify_all();
}

inline void ThreadPool::wait() {
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

inline size_t ThreadPool::size() const {
    return workers_.size();
}

inline bool ThreadPool::is_stopped() const {
    return stop_.load(std::memory_order_acquire);
}

inline void ThreadPool::worker_thread() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            condition_.wait(lock, [this] {
                return stop_ || !tasks_.empty();
            });

            if (stop_ && tasks_.empty()) {
                return;
            }

            task = std::move(tasks_.front());
            tasks_.pop();
        }
        task();
    }
}

// Thread-safe task queue with priority
template<typename T>
class PriorityTaskQueue {
    struct Task {
        T task;
        int priority;
        std::chrono::steady_clock::time_point timestamp;

        bool operator<(const Task& other) const {
            if (priority != other.priority) {
                return priority < other.priority;
            }
            return timestamp > other.timestamp;
        }
    };

    std::priority_queue<Task> queue_;
    mutable std::mutex mutex_;
    std::condition_variable not_empty_;
    std::atomic<bool> stopped_{false};

public:
    void push(T task, int priority = 0) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push({std::move(task), priority, 
                        std::chrono::steady_clock::now()});
        }
        not_empty_.notify_one();
    }

    bool pop(T& task) {
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_.wait(lock, [this] {
            return !queue_.empty() || stopped_.load(std::memory_order_acquire);
        });

        if (queue_.empty() && stopped_.load(std::memory_order_acquire)) {
            return false;
        }

        task = std::move(queue_.top().task);
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

// Thread-safe work stealing queue
template<typename T>
class WorkStealingQueue {
    static constexpr size_t CACHE_LINE_SIZE = 64;
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> head_{0};
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> tail_{0};
    std::vector<T> buffer_;
    mutable std::mutex mutex_;

public:
    explicit WorkStealingQueue(size_t capacity = 1024)
        : buffer_(capacity) {}

    bool push(T item) {
        size_t head = head_.load(std::memory_order_relaxed);
        size_t tail = tail_.load(std::memory_order_relaxed);
        size_t next = (tail + 1) % buffer_.size();

        if (next == head) {
            return false;
        }

        buffer_[tail] = std::move(item);
        tail_.store(next, std::memory_order_release);
        return true;
    }

    bool pop(T& item) {
        size_t head = head_.load(std::memory_order_relaxed);
        size_t tail = tail_.load(std::memory_order_acquire);
        
        if (head == tail) {
            return false;
        }

        item = std::move(buffer_[head]);
        head_.store((head + 1) % buffer_.size(), std::memory_order_release);
        return true;
    }

    bool steal(T& item) {
        size_t head = head_.load(std::memory_order_acquire);
        size_t tail = tail_.load(std::memory_order_relaxed);
        
        if (head >= tail) {
            return false;
        }

        item = std::move(buffer_[head]);
        head_.store((head + 1) % buffer_.size(), std::memory_order_release);
        return true;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return head_.load(std::memory_order_acquire) == 
               tail_.load(std::memory_order_acquire);
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return (tail_.load(std::memory_order_acquire) - 
                head_.load(std::memory_order_acquire)) % buffer_.size();
    }
};

// Implementation of missing methods
template<typename F, typename... Args>
auto ThreadPool::submitTask(F&& f, Args&&... args) 
    -> std::future<typename std::invoke_result<F, Args...>::type> {
    return submit(std::forward<F>(f), std::forward<Args>(args)...);
}

void ThreadPool::start() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (!stop_.load(std::memory_order_acquire)) {
        pause_.store(false, std::memory_order_release);
        condition_.notify_all();
    }
}

void ThreadPool::pause() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    pause_.store(true, std::memory_order_release);
}

void ThreadPool::resume() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    pause_.store(false, std::memory_order_release);
    condition_.notify_all();
}

void ThreadPool::setThreadCount(size_t count) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (count == workers_.size()) {
        return;
    }

    stop();
    wait();

    workers_.clear();
    for (size_t i = 0; i < count; ++i) {
        workers_.emplace_back(&ThreadPool::worker_thread, this);
    }
}

ThreadPool::ThreadMetrics ThreadPool::getMetrics() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    return metrics_;
}

size_t ThreadPool::getActiveThreads() const {
    return active_threads_.load(std::memory_order_acquire);
}

size_t ThreadPool::getPendingTasks() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return tasks_.size();
}

double ThreadPool::getUtilization() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    if (metrics_.total_tasks == 0) {
        return 0.0;
    }
    return static_cast<double>(metrics_.completed_tasks) / metrics_.total_tasks;
}

void ThreadPool::setMaxQueueSize(size_t size) {
    max_queue_size_.store(size, std::memory_order_release);
}

void ThreadPool::setTaskTimeout(std::chrono::milliseconds timeout) {
    task_timeout_.store(timeout, std::memory_order_release);
}

void ThreadPool::updateMetrics(const std::string& operation, std::chrono::milliseconds duration) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    metrics_.total_tasks++;
    if (operation == "completed") {
        metrics_.completed_tasks++;
    } else if (operation == "failed") {
        metrics_.failed_tasks++;
    }
    metrics_.average_task_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        (metrics_.average_task_time * (metrics_.total_tasks - 1) + duration) / metrics_.total_tasks);
    metrics_.last_update = std::chrono::system_clock::now();
}

bool ThreadPool::shouldProcessTask(const Task& task) const {
    if (task_timeout_.load(std::memory_order_acquire).count() > 0) {
        auto now = std::chrono::system_clock::now();
        if (now - task.created_at > task_timeout_.load(std::memory_order_acquire)) {
            return false;
        }
    }
    return true;
}

void ThreadPool::cleanupStaleTasks() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    while (!tasks_.empty()) {
        const auto& task = tasks_.front();
        if (!shouldProcessTask(task)) {
            tasks_.pop();
            updateMetrics("failed", std::chrono::milliseconds(0));
        } else {
            break;
        }
    }
}

} // namespace thread
} // namespace cloud 