#include "core/thread_pool.hpp"
#include <iostream>
#include <algorithm>

namespace cloud {

ThreadPool::ThreadPool(size_t num_threads)
    : stop_(false)
    , pause_(false)
    , active_threads_(0)
    , max_queue_size_(1000)
    , task_timeout_(std::chrono::seconds(30)) {
    
    metrics_.total_tasks = 0;
    metrics_.completed_tasks = 0;
    metrics_.failed_tasks = 0;
    metrics_.last_update = std::chrono::system_clock::now();
    metrics_.average_task_time = std::chrono::milliseconds(0);
    
    workers_.reserve(num_threads);
    start();
}

ThreadPool::~ThreadPool() {
    stop();
}

template<typename F, typename... Args>
auto ThreadPool::submitTask(F&& f, Args&&... args)
    -> std::future<typename std::invoke_result<F, Args...>::type> {
    
    using return_type = typename std::invoke_result<F, Args...>::type;
    using packaged_task = std::packaged_task<return_type()>;
    
    auto task = std::make_shared<packaged_task>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );
    
    std::future<return_type> res = task->get_future();
    
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        
        // Проверяем, не превышен ли лимит очереди
        if (tasks_.size() >= max_queue_size_) {
            std::cerr << "ThreadPool: Очередь задач переполнена" << std::endl;
            metrics_.failed_tasks++;
            return std::future<return_type>();
        }
        
        tasks_.push(Task{
            [task]() { (*task)(); },
            std::chrono::system_clock::now()
        });
        
        metrics_.total_tasks++;
    }
    
    condition_.notify_one();
    return res;
}

void ThreadPool::start() {
    for (size_t i = 0; i < workers_.capacity(); ++i) {
        workers_.emplace_back(&ThreadPool::workerThread, this);
    }
}

void ThreadPool::stop() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        stop_ = true;
    }
    
    condition_.notify_all();
    pause_condition_.notify_all();
    
    for (std::thread& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    
    workers_.clear();
    cleanupStaleTasks();
}

void ThreadPool::pause() {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    pause_ = true;
    pause_condition_.notify_all();
}

void ThreadPool::resume() {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    pause_ = false;
    pause_condition_.notify_all();
}

void ThreadPool::setThreadCount(size_t count) {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    
    if (count == workers_.size()) {
        return;
    }
    
    if (count < workers_.size()) {
        // Уменьшаем количество потоков
        stop_ = true;
        condition_.notify_all();
        
        for (size_t i = count; i < workers_.size(); ++i) {
            if (workers_[i].joinable()) {
                workers_[i].join();
            }
        }
        
        workers_.resize(count);
        stop_ = false;
    } else {
        // Увеличиваем количество потоков
        size_t old_size = workers_.size();
        workers_.reserve(count);
        
        for (size_t i = old_size; i < count; ++i) {
            workers_.emplace_back(&ThreadPool::workerThread, this);
        }
    }
}

ThreadPool::ThreadMetrics ThreadPool::getMetrics() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    return metrics_;
}

size_t ThreadPool::getActiveThreads() const {
    return active_threads_;
}

size_t ThreadPool::getPendingTasks() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return tasks_.size();
}

double ThreadPool::getUtilization() const {
    return static_cast<double>(active_threads_) / workers_.size();
}

void ThreadPool::setMaxQueueSize(size_t size) {
    max_queue_size_ = size;
}

void ThreadPool::setTaskTimeout(std::chrono::milliseconds timeout) {
    task_timeout_ = timeout;
}

void ThreadPool::workerThread() {
    while (true) {
        Task task;
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            
            condition_.wait(lock, [this] {
                return stop_ || (!pause_ && !tasks_.empty());
            });
            
            if (stop_ && tasks_.empty()) {
                return;
            }
            
            if (tasks_.empty()) {
                continue;
            }
            
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        
        if (!shouldProcessTask(task)) {
            metrics_.failed_tasks++;
            continue;
        }
        
        active_threads_++;
        auto start_time = std::chrono::system_clock::now();
        
        try {
            task.func();
            metrics_.completed_tasks++;
        } catch (...) {
            metrics_.failed_tasks++;
        }
        
        auto end_time = std::chrono::system_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time
        );
        
        updateMetrics("task_completion", duration);
        active_threads_--;
    }
}

void ThreadPool::updateMetrics(const std::string& operation, std::chrono::milliseconds duration) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    
    metrics_.last_update = std::chrono::system_clock::now();
    
    if (operation == "task_completion") {
        // Обновляем среднее время выполнения задачи
        size_t total_tasks = metrics_.completed_tasks + metrics_.failed_tasks;
        metrics_.average_task_time = std::chrono::milliseconds(
            (metrics_.average_task_time.count() * (total_tasks - 1) + duration.count()) / total_tasks
        );
    }
}

bool ThreadPool::shouldProcessTask(const Task& task) const {
    auto now = std::chrono::system_clock::now();
    auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - task.created_at
    );
    
    return age <= task_timeout_;
}

void ThreadPool::cleanupStaleTasks() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    while (!tasks_.empty()) {
        const auto& task = tasks_.front();
        if (!shouldProcessTask(task)) {
            metrics_.failed_tasks++;
        }
        tasks_.pop();
    }
}

} // namespace cloud 