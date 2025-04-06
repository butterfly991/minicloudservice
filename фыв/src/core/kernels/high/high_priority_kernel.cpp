#include "core/kernels/high/high_priority_kernel.hpp"
#include <algorithm>
#include <chrono>

namespace cloud {
namespace core {
namespace kernels {
namespace high {

class HighPriorityKernel {
public:
    HighPriorityKernel(std::shared_ptr<Logger> logger, 
                       std::shared_ptr<ResourceManager> resource_manager, 
                       std::shared_ptr<TaskManager> task_manager)
        : logger_(std::move(logger)), 
          resource_manager_(std::move(resource_manager)), 
          task_manager_(std::move(task_manager)) {
        if (!logger_) throw std::invalid_argument("Logger cannot be null");
        if (!resource_manager_) throw std::invalid_argument("ResourceManager cannot be null");
        if (!task_manager_) throw std::invalid_argument("TaskManager cannot be null");
    }

    void boost_priority(const std::string& task_id);
    void preempt_task(const std::string& task_id);
    void worker_thread();

private:
    std::mutex mutex_;
    std::atomic<bool> is_running_{true};
    std::atomic<bool> is_paused_{false};
    std::atomic<int> active_threads_{0};
    std::shared_ptr<Logger> logger_;
    std::shared_ptr<ResourceManager> resource_manager_;
    std::shared_ptr<TaskManager> task_manager_;
    std::vector<Task> tasks_; // Предполагается, что Task - это структура или класс

    void update_metrics(const std::string& operation, const std::string& task_state);
};

void HighPriorityKernel::boost_priority(const std::string& task_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto task_it = std::find_if(tasks_.begin(), tasks_.end(),
        [&task_id](const auto& task) { return task.get_id() == task_id; });

    if (task_it != tasks_.end()) {
        auto task = *task_it;
        tasks_.erase(task_it);
        tasks_.emplace_front(task);
        task.set_high_priority(true);
        update_metrics("boosted", task.get_state());
        logger_->log_info("HighPriorityKernel", "Task " + task_id + " priority boosted.");
    } else {
        logger_->log_warning("HighPriorityKernel", "Task " + task_id + " not found for boosting priority.");
    }
}

void HighPriorityKernel::preempt_task(const std::string& task_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto task_it = std::find_if(tasks_.begin(), tasks_.end(),
        [&task_id](const auto& task) { return task.get_id() == task_id; });

    if (task_it != tasks_.end()) {
        auto task_state = task_it->get_state();
        tasks_.erase(task_it);
        update_metrics("preempted", task_state);
        logger_->log_info("HighPriorityKernel", "Task " + task_id + " has been preempted.");
    } else {
        logger_->log_warning("HighPriorityKernel", "Task " + task_id + " not found for preemption.");
    }
}

void HighPriorityKernel::worker_thread() {
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
            if (is_timed_out(start_time)) {
                logger_->log_warning("HighPriorityKernel", "Task timed out.");
                return;
            }

            if (!check_resources()) {
                logger_->log_warning("HighPriorityKernel", "Insufficient resources to execute task.");
                return;
            }

            logger_->log_info("HighPriorityKernel", "Starting task execution.");
            task();
            auto end_time = std::chrono::steady_clock::now();
            update_metrics("completed", end_time - start_time);
            logger_->log_info("HighPriorityKernel", "Task executed successfully.");
        }
        catch (const std::exception& e) {
            logger_->log_error("HighPriorityKernel", "Error executing task: " + std::string(e.what()));
            auto end_time = std::chrono::steady_clock::now();
            update_metrics("failed", end_time - start_time, e.what());
        }
        task_count_.fetch_sub(1, std::memory_order_release);
    }

    active_threads_.fetch_sub(1, std::memory_order_release);
    if (is_shutting_down_.load(std::memory_order_acquire)) {
        shutdown_condition_.notify_one();
    }
}

} // namespace high
} // namespace kernels
} // namespace core
} // namespace cloud 