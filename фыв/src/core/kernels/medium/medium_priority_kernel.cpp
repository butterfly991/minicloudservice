#include "core/kernels/medium/medium_priority_kernel.hpp"
#include <algorithm>
#include <chrono>
#include <queue>

namespace cloud {
namespace core {
namespace kernels {
namespace medium {

class MediumPriorityKernel {
public:
    MediumPriorityKernel(std::shared_ptr<Logger> logger, 
                         std::shared_ptr<ResourceManager> resource_manager, 
                         std::shared_ptr<TaskManager> task_manager)
        : logger_(std::move(logger)), 
          resource_manager_(std::move(resource_manager)), 
          task_manager_(std::move(task_manager)) {
        if (!logger_) throw std::invalid_argument("Logger cannot be null");
        if (!resource_manager_) throw std::invalid_argument("ResourceManager cannot be null");
        if (!task_manager_) throw std::invalid_argument("TaskManager cannot be null");
    }

    void process_batch();
    void prioritize_task(const std::string& task_id, int priority);
    void worker_thread();

private:
    std::mutex mutex_;
    std::atomic<bool> is_running_{true};
    std::atomic<bool> is_paused_{false};
    std::atomic<int> active_threads_{0};
    std::shared_ptr<Logger> logger_;
    std::shared_ptr<ResourceManager> resource_manager_;
    std::shared_ptr<TaskManager> task_manager_;
    MediumPriorityConfig medium_priority_config_;
    std::vector<std::function<void()>> tasks_;

    void update_metrics(const std::string& operation, const std::string& task_state);
};

void MediumPriorityKernel::process_batch() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!medium_priority_config_.enable_batch_processing) {
        return;
    }

    std::vector<std::function<void()>> batch;
    while (!tasks_.empty() && batch.size() < medium_priority_config_.batch_size) {
        batch.push_back(std::move(tasks_.front()));
        tasks_.pop();
    }

    if (batch.empty()) {
        return;
    }

    double current_cpu = resource_manager_->get_max_cpu();
    size_t current_memory = resource_manager_->get_max_memory();
    resource_manager_->check_resources();

    auto start_time = std::chrono::steady_clock::now();
    try {
        for (auto& task : batch) {
            task();
        }
        auto end_time = std::chrono::steady_clock::now();
        update_metrics("batch_completed", end_time - start_time);
    }
    catch (const std::exception& e) {
        auto end_time = std::chrono::steady_clock::now();
        update_metrics("batch_failed", end_time - start_time, e.what());
        logger_->log_error("MediumPriorityKernel", "Batch processing failed: " + std::string(e.what()));
    }
}

void MediumPriorityKernel::prioritize_task(const std::string& task_id, int priority) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!medium_priority_config_.enable_task_prioritization) {
        return;
    }

    auto task_it = std::find_if(tasks_.begin(), tasks_.end(),
        [&task_id](const auto& task) { return task.id == task_id; });

    if (task_it != tasks_.end()) {
        Task task = *task_it;
        task.priority = priority;
        tasks_.erase(task_it);

        auto insert_pos = std::find_if(tasks_.begin(), tasks_.end(), [&](const Task& t) {
            return t.priority < task.priority;
        });

        tasks_.insert(insert_pos, task);
        condition_.notify_one();
        update_metrics("priority_updated");
    } else {
        logger_->log_error("MediumPriorityKernel", "Task not found for priority update: " + task_id);
    }
}

void MediumPriorityKernel::worker_thread() {
    active_threads_.fetch_add(1, std::memory_order_release);
    
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this] {
                return !is_running_.load(std::memory_order_acquire) || 
                       (!is_paused_.load(std::memory_order_acquire) && !tasks_.empty());
            });

            if (!is_running_.load(std::memory_order_acquire) && tasks_.empty()) {
                break;
            }

            if (is_paused_.load(std::memory_order_acquire)) {
                continue;
            }

            task = std::move(tasks_.front());
            tasks_.pop();
        }

        auto start_time = std::chrono::steady_clock::now();
        try {
            task();
            auto end_time = std::chrono::steady_clock::now();
            update_metrics("completed", end_time - start_time);
        } catch (const std::exception& e) {
            auto end_time = std::chrono::steady_clock::now();
            update_metrics("failed", end_time - start_time, e.what());
            logger_->log_error("MediumPriorityKernel", "Task execution failed: " + std::string(e.what()));
        }
        task_count_.fetch_sub(1, std::memory_order_release);
    }

    active_threads_.fetch_sub(1, std::memory_order_release);
    if (is_shutting_down_.load(std::memory_order_acquire)) {
        shutdown_condition_.notify_one();
    }
}

} // namespace medium
} // namespace kernels
} // namespace core
} // namespace cloud 