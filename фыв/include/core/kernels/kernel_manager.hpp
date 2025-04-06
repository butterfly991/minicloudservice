#pragma once

#include "base_kernel.hpp"
#include "high/high_priority_kernel.hpp"
#include "medium/medium_priority_kernel.hpp"
#include "low/low_priority_kernel.hpp"

#include <memory>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <string>
#include <functional>
#include <array>
#include <atomic>

namespace cloud {
namespace core {
namespace kernels {

class KernelManager {
public:
    struct KernelMetrics {
        BaseKernel::KernelMetrics high_priority;
        BaseKernel::KernelMetrics medium_priority;
        BaseKernel::KernelMetrics low_priority;
        std::chrono::steady_clock::time_point last_update;
        
        // Агрегированные метрики
        double total_cpu_usage{0.0};
        size_t total_memory_usage{0};
        double total_throughput{0.0};
        double total_error_rate{0.0};
        double total_utilization{0.0};
        
        // Метрики балансировки
        double load_balance_score{0.0};
        double resource_efficiency{0.0};
        double task_distribution_quality{0.0};
    };

    struct KernelConfig {
        HighPriorityKernel::HighPriorityConfig high_priority;
        MediumPriorityKernel::MediumPriorityConfig medium_priority;
        LowPriorityKernel::LowPriorityConfig low_priority;
        
        // Настройки автоматического масштабирования
        bool enable_auto_scaling{true};
        bool enable_load_balancing{true};
        bool enable_resource_optimization{true};
        
        // Пороговые значения для масштабирования
        double scale_up_threshold{0.8};    // 80% загрузки
        double scale_down_threshold{0.2};  // 20% загрузки
        double balance_threshold{0.1};     // 10% дисбаланса
        
        // Интервалы обновления
        std::chrono::milliseconds metrics_update_interval{std::chrono::seconds(60)};
        std::chrono::milliseconds health_check_interval{std::chrono::seconds(30)};
        std::chrono::milliseconds scaling_check_interval{std::chrono::seconds(300)};
        
        // Ограничения ресурсов
        double max_total_cpu_usage{0.9};  // 90% CPU
        size_t max_total_memory_usage{4 * 1024 * 1024 * 1024};  // 4GB
        size_t max_total_threads{16};
        
        // Целевые соотношения задач
        double target_high_priority_ratio{0.2};    // 20%
        double target_medium_priority_ratio{0.5};  // 50%
        double target_low_priority_ratio{0.3};     // 30%
    };

    explicit KernelManager(const KernelConfig& config = KernelConfig{});
    ~KernelManager();

    // Управление ядрами
    void start();
    void stop();

    // Отправка задач
    template<typename F, typename... Args>
    auto submit_task(F&& f, Args&&... args, int priority) 
        -> std::future<typename std::invoke_result<F, Args...>::type>;

    // Мониторинг и метрики
    KernelMetrics get_metrics() const;
    void update_metrics();

    // Состояние
    bool is_running() const;

    // Управление ресурсами
    void scale_kernels();
    void balance_load();
    void optimize_resources();
    void handle_unhealthy_kernels();

    void registerKernels(std::shared_ptr<Logger> logger,
                         std::shared_ptr<ResourceManager> resourceManager,
                         std::shared_ptr<TaskManager> taskManager);

private:
    void initialize_kernels();
    void start_monitoring();
    void stop_monitoring();
    void monitoring_loop();
    void scaling_loop();
    void check_health();
    bool check_kernel_health(BaseKernel* kernel);
    void update_aggregated_metrics();
    void update_balance_metrics();

    // Логика перераспределения задач и ресурсов
    void redistribute_tasks(const KernelMetrics& metrics);
    void adjust_task_priorities(const KernelMetrics& metrics);
    void redistribute_resources();
    
    // Вспомогательные методы
    void move_task_to_medium_priority();
    void move_task_to_low_priority();
    void promote_task_to_medium_priority();
    void demote_task_to_medium_priority();
    void allocate_resources_to_medium_priority();
    void allocate_resources_to_low_priority();

    KernelConfig config_;
    std::shared_ptr<HighPriorityKernel> high_priority_kernel_;
    std::shared_ptr<MediumPriorityKernel> medium_priority_kernel_;
    std::shared_ptr<LowPriorityKernel> low_priority_kernel_;
    
    std::atomic<bool> is_running_{false};
    mutable std::mutex mutex_;
    mutable std::mutex metrics_mutex_;
    KernelMetrics metrics_;
    
    std::thread monitoring_thread_;
    std::thread scaling_thread_;
};

template<typename F, typename... Args>
auto KernelManager::submit_task(F&& f, Args&&... args, int priority) 
        -> std::future<typename std::invoke_result<F, Args...>::type> {
    // Определяем, в какое ядро отправить задачу на основе приоритета
    if (priority >= 80) {
            return high_priority_kernel_->submit(std::forward<F>(f), std::forward<Args>(args)...);
        } else if (priority >= 50) {
            return medium_priority_kernel_->submit(std::forward<F>(f), std::forward<Args>(args)...);
        } else {
            return low_priority_kernel_->submit(std::forward<F>(f), std::forward<Args>(args)...);
        }
    }

void KernelManager::start() {
        std::lock_guard<std::mutex> lock(mutex_);
    if (!is_running_.load()) {
        high_priority_kernel_->start();
        medium_priority_kernel_->start();
        low_priority_kernel_->start();
        is_running_.store(true);
        // Запуск потоков мониторинга и масштабирования
        monitoring_thread_ = std::thread(&KernelManager::monitoring_loop, this);
        scaling_thread_ = std::thread(&KernelManager::scaling_loop, this);
    }
}

void KernelManager::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (is_running_.load()) {
        high_priority_kernel_->stop();
        medium_priority_kernel_->stop();
        low_priority_kernel_->stop();
        is_running_.store(false);
        // Ожидание завершения потоков
        if (monitoring_thread_.joinable()) {
            monitoring_thread_.join();
        }
        if (scaling_thread_.joinable()) {
            scaling_thread_.join();
        }
    }
}

void KernelManager::initialize_kernels() {
    high_priority_kernel_ = std::make_shared<HighPriorityKernel>(config_.high_priority);
    medium_priority_kernel_ = std::make_shared<MediumPriorityKernel>(config_.medium_priority);
    low_priority_kernel_ = std::make_shared<LowPriorityKernel>(config_.low_priority);
}

void KernelManager::monitoring_loop() {
        while (is_running_.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(config_.health_check_interval);
        check_health();
        update_metrics();
    }
}

void KernelManager::handle_unhealthy_kernels() {
    if (!check_kernel_health(high_priority_kernel_.get())) {
        logger_->log_error("KernelManager", "High priority kernel not healthy");
    }
    if (!check_kernel_health(medium_priority_kernel_.get())) {
        logger_->log_error("KernelManager", "Medium priority kernel not healthy");
    }
    if (!check_kernel_health(low_priority_kernel_.get())) {
        logger_->log_error("KernelManager", "Low priority kernel not healthy");
    }
}

void KernelManager::registerKernels(std::shared_ptr<Logger> logger,
                                     std::shared_ptr<ResourceManager> resourceManager,
                                     std::shared_ptr<TaskManager> taskManager) {
    high_priority_kernel_ = std::make_shared<HighPriorityKernel>(logger, resourceManager, taskManager);
    medium_priority_kernel_ = std::make_shared<MediumPriorityKernel>(logger, resourceManager, taskManager);
    low_priority_kernel_ = std::make_shared<LowPriorityKernel>(logger, resourceManager, taskManager);
}

} // namespace kernels
} // namespace core
} // namespace cloud 