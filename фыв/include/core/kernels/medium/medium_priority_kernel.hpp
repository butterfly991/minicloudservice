#pragma once

#include "../base_kernel.hpp"

namespace cloud {
namespace core {
namespace kernels {
namespace medium {

class MediumPriorityKernel : public BaseKernel {
public:
    struct MediumPriorityConfig : public BaseKernel::KernelConfig {
        size_t max_concurrent_tasks{500};
        std::chrono::milliseconds task_timeout{std::chrono::seconds(30)};
        bool enable_batch_processing{true};
        size_t batch_size{10};
        std::chrono::milliseconds batch_timeout{std::chrono::seconds(5)};
        bool enable_task_prioritization{true};
    };

    MediumPriorityKernel(std::shared_ptr<Logger> logger,
                         std::shared_ptr<ResourceManager> resourceManager,
                         std::shared_ptr<TaskManager> taskManager,
                         const MediumPriorityConfig& config = MediumPriorityConfig{})
        : BaseKernel(config), logger_(logger), resourceManager_(resourceManager), taskManager_(taskManager) {
        config_.priority = 50;
        config_.max_tasks = 5000;
        config_.num_threads = 4;
        config_.enable_metrics = true;
        config_.enable_tracing = true;
        config_.name = "MediumPriorityKernel";
    }

    // Специфичные для среднего приоритета методы
    virtual void process_batch() {
        std::lock_guard<std::mutex> lock(mutex_);
        // Реализация пакетной обработки
    }

    virtual void prioritize_task(const std::string& task_id, int priority) {
        std::lock_guard<std::mutex> lock(mutex_);
        // Реализация приоритизации задачи
    }

    // Переопределение базовых методов для специфичной логики
    virtual void start() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!is_running_.load(std::memory_order_acquire)) {
            // Дополнительная инициализация для среднего приоритета
            is_running_.store(true, std::memory_order_release);
            is_paused_.store(false, std::memory_order_release);
            is_shutting_down_.store(false, std::memory_order_release);
            condition_.notify_all();
        }
    }

protected:
    virtual void worker_thread() override {
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
                // Дополнительная логика для среднего приоритета
                task();
                auto end_time = std::chrono::steady_clock::now();
                update_metrics("completed", end_time - start_time);
            }
            catch (const std::exception& e) {
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

private:
    MediumPriorityConfig medium_priority_config_;
    std::shared_ptr<Logger> logger_;
    std::shared_ptr<ResourceManager> resourceManager_;
    std::shared_ptr<TaskManager> taskManager_;
};

} // namespace medium
} // namespace kernels
} // namespace core
} // namespace cloud 