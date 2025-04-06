#pragma once

#include "core/kernels/low/low_priority_kernel.hpp"
#include <algorithm>
#include <chrono>
#include <thread>
#include <queue>
#include <sys/resource.h>
#include <sys/sysinfo.h>
#include <unistd.h>
#include <memory>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <sstream>
#include <vector>

namespace cloud {
namespace core {
namespace kernels {
namespace low {

class LowPriorityKernel {
public:
    LowPriorityKernel(std::shared_ptr<Logger> logger, 
                      std::shared_ptr<ResourceManager> resource_manager, 
                      std::shared_ptr<TaskManager> task_manager)
        : logger_(std::move(logger)), 
          resource_manager_(std::move(resource_manager)), 
          task_manager_(std::move(task_manager)) {
        if (!logger_) throw std::invalid_argument("Logger cannot be null");
        if (!resource_manager_) throw std::invalid_argument("ResourceManager cannot be null");
        if (!task_manager_) throw std::invalid_argument("TaskManager cannot be null");
    }

    void process_background_tasks();
    void check_idle_tasks();
    void throttle_resources();
    void worker_thread();
    void set_active_thread_count(size_t new_count);
    void clear_cache();

private:
    std::mutex background_mutex_;
    std::queue<BackgroundTask> background_tasks_;
    std::atomic<bool> background_running_{true};
    std::atomic<bool> is_paused_{false};
    std::atomic<bool> is_shutting_down_{false};
    std::condition_variable condition_;
    std::atomic<int> active_threads_{0};
    std::shared_ptr<Logger> logger_;
    std::shared_ptr<ResourceManager> resource_manager_;
    std::shared_ptr<TaskManager> task_manager_;
    ResourceLimits resource_limits_;
    Config config_;
    BackgroundStats background_stats_;
    std::vector<std::thread> threads_;

    void update_background_stats(const std::string& operation,
                                  std::chrono::steady_clock::duration duration);
    double get_current_cpu_usage() const;
    size_t get_current_memory_usage() const;
    void apply_resource_limits(double current_cpu, size_t current_memory);
};

namespace detail {

void log_resource_throttling(const std::shared_ptr<Logger>& logger, const Config& config, double current_cpu, size_t current_memory, const ResourceLimits& resource_limits) {
    std::stringstream ss;
    ss << "Resource throttling applied:\n"
       << "  CPU: " << current_cpu << "% (limit: " << resource_limits.max_cpu << "%)\n"
       << "  Memory: " << current_memory << " bytes (limit: " 
       << resource_limits.max_memory << " bytes)";
    logger->log_diagnostic(config.name, "resource_throttling", ss.str());
}

} // namespace detail

void LowPriorityKernel::process_background_tasks() {
    while (background_running_) {
        std::vector<BackgroundTask> tasks_to_process;

        {
            std::lock_guard<std::mutex> lock(background_mutex_);
            if (!config_.enable_background_processing) {
                return;
            }

            while (!background_tasks_.empty() && tasks_to_process.size() < config_.batch_size) {
                tasks_to_process.push_back(background_tasks_.front());
                background_tasks_.pop();
            }
        }

        for (const auto& task : tasks_to_process) {
            auto start_time = std::chrono::steady_clock::now();
            try {
                (*task.task)(); // Выполнение задачи
                auto end_time = std::chrono::steady_clock::now();
                update_background_stats("completed", end_time - start_time);
            } catch (const std::exception& e) {
                auto end_time = std::chrono::steady_clock::now();
                update_background_stats("failed", end_time - start_time);
                logger_->log_error(config_.name, "Background task failed: " + std::string(e.what()));
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Задержка для предотвращения активного ожидания
    }
}

void LowPriorityKernel::check_idle_tasks() {
    std::lock_guard<std::mutex> lock(background_mutex_);
    
    if (!config_.enable_idle_processing) {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    auto idle_threshold = now - config_.idle_check_interval;

    for (auto it = background_tasks_.begin(); it != background_tasks_.end();) {
        if (it->deadline < idle_threshold) {
            logger_->log(KernelLogger::LogLevel::WARNING, config_.name,
                         "Idle task detected and will be removed.");
            it = background_tasks_.erase(it);
        } else {
            ++it;
        }
    }
}

void LowPriorityKernel::throttle_resources() {
    std::lock_guard<std::mutex> lock(background_mutex_);
    
    if (!config_.enable_resource_throttling) {
        return;
    }

    double current_cpu = get_current_cpu_usage();
    size_t current_memory = get_current_memory_usage();

    if (current_cpu > resource_limits_.max_cpu || 
        current_memory > resource_limits_.max_memory) {
        apply_resource_limits(current_cpu, current_memory);
        detail::log_resource_throttling(logger_, config_, current_cpu, current_memory, resource_limits_);
    }
}

void LowPriorityKernel::worker_thread() {
    active_threads_.fetch_add(1, std::memory_order_release);
    
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(background_mutex_);
            condition_.wait(lock, [this] {
                return !background_running_.load(std::memory_order_acquire) || 
                       (!is_paused_.load(std::memory_order_acquire) && !background_tasks_.empty());
            });

            if (!background_running_.load(std::memory_order_acquire) && background_tasks_.empty()) {
                break;
            }

            if (is_paused_.load(std::memory_order_acquire)) {
                continue;
            }

            task = std::move(background_tasks_.front().task);
            background_tasks_.pop();
        }

        auto start_time = std::chrono::steady_clock::now();
        try {
            task(); // Выполнение задачи
            auto end_time = std::chrono::steady_clock::now();
            update_background_stats("completed", end_time - start_time);
        } catch (const std::exception& e) {
            auto end_time = std::chrono::steady_clock::now();
            update_background_stats("failed", end_time - start_time);
            logger_->log_error(config_.name, "Background task failed: " + std::string(e.what()));
        }
    }

    active_threads_.fetch_sub(1, std::memory_order_release);
    if (is_shutting_down_.load(std::memory_order_acquire)) {
        condition_.notify_one();
    }
}

double LowPriorityKernel::get_current_cpu_usage() const {
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return 0.0;
    }

    double user_time = usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1000000.0;
    double system_time = usage.ru_stime.tv_sec + usage.ru_stime.tv_usec / 1000000.0;
    
    long num_processors = sysconf(_SC_NPROCESSORS_ONLN);
    if (num_processors <= 0) {
        return 0.0;
    }

    return (user_time + system_time) * 100.0 / num_processors;
}

size_t LowPriorityKernel::get_current_memory_usage() const {
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return 0;
    }

    return usage.ru_maxrss * 1024;  // Конвертируем из килобайт в байты
}

void LowPriorityKernel::apply_resource_limits(double current_cpu, size_t current_memory) {
    if (current_cpu > resource_limits_.max_cpu) {
        size_t current_threads = active_threads_.load(std::memory_order_acquire);
        if (current_threads > 1) {
            size_t new_thread_count = std::max(size_t(1), 
                static_cast<size_t>(current_threads * (resource_limits_.max_cpu / current_cpu)));
            set_active_thread_count(new_thread_count);
            logger_->log(KernelLogger::LogLevel::WARNING, config_.name,
                         "Reduced active threads to " + std::to_string(new_thread_count) +
                         " due to high CPU usage.");
        }
    }

    if (current_memory > resource_limits_.max_memory) {
        std::lock_guard<std::mutex> lock(background_mutex_);
        config_.enable_background_processing = false;
        logger_->log(KernelLogger::LogLevel::WARNING, config_.name,
                     "Background processing suspended due to high memory usage");
    }

    if (current_memory > resource_limits_.max_memory * 0.9) {
        clear_cache();
        logger_->log(KernelLogger::LogLevel::WARNING, config_.name,
                     "Cache cleared due to high memory usage.");
    }

    if (current_cpu > resource_limits_.max_cpu * 0.9 || 
        current_memory > resource_limits_.max_memory * 0.9) {
        std::lock_guard<std::mutex> lock(background_mutex_);
        size_t new_queue_size = background_tasks_.size() / 2;
        while (background_tasks_.size() > new_queue_size) {
            background_tasks_.pop();
            update_background_stats("cancelled");
        }
        
        logger_->log(KernelLogger::LogLevel::WARNING, config_.name,
                     "Background task queue reduced due to resource constraints");
    }
}

void LowPriorityKernel::set_active_thread_count(size_t new_count) {
    std::lock_guard<std::mutex> lock(background_mutex_);
    
    size_t current_threads = active_threads_.load(std::memory_order_acquire);

    if (new_count > current_threads) {
        for (size_t i = current_threads; i < new_count; ++i) {
            threads_.emplace_back([this]() { 
                worker_thread(); 
            });
        }
        active_threads_.fetch_add(new_count - current_threads, std::memory_order_release);
    } 
    else if (new_count < current_threads) {
        is_shutting_down_.store(true, std::memory_order_release);
        condition_.notify_all();

        for (size_t i = new_count; i < current_threads; ++i) {
            if (threads_[i].joinable()) {
                threads_[i].join();
            }
        }
        threads_.resize(new_count);
    }

    logger_->log_info(config_.name, "Active thread count set to " + std::to_string(new_count));
}

void LowPriorityKernel::clear_cache() {
    std::lock_guard<std::mutex> lock(background_mutex_);
    
    if (!background_tasks_.empty()) {
        logger_->log_info(config_.name, "Clearing cached background tasks.");
        while (!background_tasks_.empty()) {
            background_tasks_.pop();
        }
    }

    logger_->log_info(config_.name, "Cache cleared.");
}

void LowPriorityKernel::update_background_stats(const std::string& operation,
                               std::chrono::steady_clock::duration duration) {
    std::lock_guard<std::mutex> lock(background_mutex_);
    
    background_stats_.total_background_tasks++;
    
    if (operation == "completed") {
        background_stats_.completed_background_tasks++;
        auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration);
        background_stats_.average_background_task_time = 
            (background_stats_.average_background_task_time * 
             (background_stats_.completed_background_tasks - 1) + duration_ms) / 
            background_stats_.completed_background_tasks;
    } else if (operation == "failed") {
        background_stats_.failed_background_tasks++;
    } else if (operation == "timed_out") {
        background_stats_.timed_out_background_tasks++;
    } else if (operation == "cancelled") {
        // Обработка отмененных задач
    }

    logger_->log_info("LowPriorityKernel", "Background task operation: " + operation);
}

} // namespace low
} // namespace kernels
} // namespace core
} // namespace cloud 