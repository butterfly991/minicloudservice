#pragma once

#include "../../directives.hpp"
#include "../../sync_utils.hpp"
#include "../../thread_pool.hpp"
#include "../../smart_pointers.hpp"
#include "../../function_wrappers.hpp"
#include "kernel_logger.hpp"

#include <vector>
#include <memory>
#include <functional>
#include <chrono>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <string>
#include <unordered_map>
#include <array>
#include <sstream>

namespace cloud {
namespace core {
namespace kernels {

// Базовый класс для всех ядер
class BaseKernel {
protected:
    struct KernelMetrics {
        // Основные метрики
        size_t total_tasks{0};
        size_t completed_tasks{0};
        size_t failed_tasks{0};
        size_t cancelled_tasks{0};
        
        // Метрики времени
        std::chrono::milliseconds average_task_time{0};
        std::chrono::milliseconds max_task_time{0};
        std::chrono::milliseconds min_task_time{std::chrono::milliseconds::max()};
        std::chrono::steady_clock::time_point last_update;
        
        // Метрики по типам задач
        std::unordered_map<std::string, size_t> task_types;
        std::unordered_map<std::string, size_t> error_types;
        
        // Метрики ресурсов
        double cpu_usage{0.0};
        size_t memory_usage{0};
        size_t peak_memory_usage{0};
        
        // Метрики очереди
        size_t current_queue_size{0};
        size_t max_queue_size{0};
        std::chrono::milliseconds average_wait_time{0};
        
        // Метрики потоков
        size_t active_threads{0};
        size_t idle_threads{0};
        size_t blocked_threads{0};
        
        // Метрики производительности
        double throughput{0.0};  // задач в секунду
        double error_rate{0.0};  // процент ошибок
        double utilization{0.0}; // процент использования ресурсов
    };

    struct KernelConfig {
        // Основные настройки
        size_t max_tasks{1000};
        size_t num_threads{2};
        std::chrono::milliseconds task_timeout{std::chrono::seconds(5)};
        
        // Настройки мониторинга
        bool enable_metrics{true};
        bool enable_tracing{false};
        bool enable_performance_monitoring{true};
        bool enable_resource_monitoring{true};
        
        // Настройки метрик
        std::chrono::milliseconds metrics_update_interval{std::chrono::seconds(1)};
        size_t metrics_history_size{1000};
        
        // Настройки ресурсов
        double max_cpu_usage{0.8};  // 80% CPU
        size_t max_memory_usage{1024 * 1024 * 1024};  // 1GB
        
        // Настройки очереди
        size_t max_queue_size{10000};
        std::chrono::milliseconds max_wait_time{std::chrono::seconds(30)};
        
        // Идентификация
        std::string name;
        int priority{0};
    };

    std::atomic<bool> is_running_{false};
    std::atomic<bool> is_paused_{false};
    std::atomic<bool> is_shutting_down_{false};
    std::atomic<int> priority_{0};
    std::atomic<size_t> task_count_{0};
    std::atomic<size_t> active_threads_{0};
    std::chrono::steady_clock::time_point last_activity_;
    
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::condition_variable shutdown_condition_;
    
    KernelMetrics metrics_;
    KernelConfig config_;
    mutable std::mutex metrics_mutex_;
    
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    
    // Система безопасности
    struct SecurityContext {
        bool require_authentication{false};
        bool require_authorization{false};
        std::vector<std::string> allowed_roles;
        std::vector<std::string> allowed_ips;
        std::chrono::seconds session_timeout{std::chrono::seconds(3600)};
    } security_;

    // Добавляем новые поля для метрик
    std::array<KernelMetrics, 1000> metrics_history_;
    size_t metrics_history_index_{0};
    std::chrono::steady_clock::time_point last_metrics_update_;
    
    // Добавляем логгер
    std::unique_ptr<KernelLogger> logger_;
    
public:
    explicit BaseKernel(const KernelConfig& config = KernelConfig{})
        : config_(config) {
        priority_.store(config.priority, std::memory_order_release);
        initialize_logger();
        initialize_workers();
    }

    virtual ~BaseKernel() {
        stop();
    }

    // Основные операции управления
    virtual void start() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!is_running_.load(std::memory_order_acquire)) {
            is_running_.store(true, std::memory_order_release);
            is_paused_.store(false, std::memory_order_release);
            is_shutting_down_.store(false, std::memory_order_release);
            condition_.notify_all();
        }
    }

    virtual void stop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (is_running_.load(std::memory_order_acquire)) {
            is_running_.store(false, std::memory_order_release);
            is_shutting_down_.store(true, std::memory_order_release);
            condition_.notify_all();
            shutdown_condition_.wait(lock, [this] {
                return active_threads_.load(std::memory_order_acquire) == 0;
            });
            for (auto& worker : workers_) {
                if (worker.joinable()) {
                    worker.join();
                }
            }
        }
    }

    virtual void pause() {
        std::lock_guard<std::mutex> lock(mutex_);
        is_paused_.store(true, std::memory_order_release);
    }

    virtual void resume() {
        std::lock_guard<std::mutex> lock(mutex_);
        is_paused_.store(false, std::memory_order_release);
        condition_.notify_all();
    }

    // Отправка задач
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
            std::lock_guard<std::mutex> lock(mutex_);
            if (!is_running_.load(std::memory_order_acquire)) {
                throw std::runtime_error("submit on stopped kernel");
            }
            tasks_.push([task]() { (*task)(); });
            task_count_.fetch_add(1, std::memory_order_release);
        }
        condition_.notify_one();
        return res;
    }

    // Безопасность
    virtual void configure_security(const SecurityContext& context) {
        std::lock_guard<std::mutex> lock(mutex_);
        security_ = context;
    }

    virtual bool authenticate(const std::string& token) {
        if (!security_.require_authentication) {
            return true;
        }
        // Реализация аутентификации
        return false;
    }

    virtual bool authorize(const std::string& role) {
        if (!security_.require_authorization) {
            return true;
        }
        return std::find(security_.allowed_roles.begin(), 
                        security_.allowed_roles.end(), 
                        role) != security_.allowed_roles.end();
    }

    // Метрики и мониторинг
    virtual KernelMetrics get_metrics() const {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        return metrics_;
    }

    virtual void reset_metrics() {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        metrics_ = KernelMetrics{};
    }

    // Состояние
    virtual bool is_running() const { 
        return is_running_.load(std::memory_order_acquire); 
    }
    
    virtual bool is_paused() const { 
        return is_paused_.load(std::memory_order_acquire); 
    }
    
    virtual int get_priority() const { 
        return priority_.load(std::memory_order_acquire); 
    }
    
    virtual size_t get_task_count() const { 
        return task_count_.load(std::memory_order_acquire); 
    }

    virtual size_t get_active_threads() const {
        return active_threads_.load(std::memory_order_acquire);
    }

protected:
    void initialize_logger() {
        KernelLogger::LoggerConfig logger_config;
        logger_config.log_file_prefix = "kernel_" + config_.name;
        logger_config.enable_metrics_logging = config_.enable_metrics;
        logger_config.enable_diagnostics = config_.enable_tracing;
        logger_ = std::make_unique<KernelLogger>(logger_config);
    }

    virtual void initialize_workers() {
        for (size_t i = 0; i < config_.num_threads; ++i) {
            workers_.emplace_back(&BaseKernel::worker_thread, this);
        }
    }

    virtual void worker_thread() {
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
                logger_->log(KernelLogger::LogLevel::DEBUG, config_.name,
                           "Task completed successfully");
            }
            catch (const std::exception& e) {
                auto end_time = std::chrono::steady_clock::now();
                update_metrics("failed", end_time - start_time, e.what());
                logger_->log_error(config_.name, "Task execution failed", e);
            }
            task_count_.fetch_sub(1, std::memory_order_release);
        }

        active_threads_.fetch_sub(1, std::memory_order_release);
        if (is_shutting_down_.load(std::memory_order_acquire)) {
            shutdown_condition_.notify_one();
        }
    }

    // Методы для работы с метриками
    virtual void update_resource_metrics() {
        if (!config_.enable_resource_monitoring) {
            return;
        }
        
        // Обновление метрик CPU
        // TODO: Реализовать получение реальных метрик CPU
        metrics_.cpu_usage = 0.0;
        
        // Обновление метрик памяти
        // TODO: Реализовать получение реальных метрик памяти
        metrics_.memory_usage = 0;
        metrics_.peak_memory_usage = std::max(metrics_.peak_memory_usage, metrics_.memory_usage);
        
        // Обновление метрик очереди
        metrics_.current_queue_size = tasks_.size();
        metrics_.max_queue_size = std::max(metrics_.max_queue_size, metrics_.current_queue_size);
        
        // Логируем диагностическую информацию
        if (metrics_.cpu_usage > config_.max_cpu_usage ||
            metrics_.memory_usage > config_.max_memory_usage) {
            std::stringstream ss;
            ss << "Resource usage warning:\n"
               << "  CPU: " << metrics_.cpu_usage << "%\n"
               << "  Memory: " << metrics_.memory_usage << " bytes";
            logger_->log_diagnostic(config_.name, "resource_usage", ss.str());
        }
    }

    virtual void update_performance_metrics() {
        if (!config_.enable_performance_monitoring) {
            return;
        }
        
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - last_metrics_update_).count();
            
        if (elapsed > 0) {
            metrics_.throughput = static_cast<double>(metrics_.completed_tasks) / elapsed;
            metrics_.error_rate = static_cast<double>(metrics_.failed_tasks) / 
                                metrics_.total_tasks * 100.0;
            metrics_.utilization = metrics_.cpu_usage * 100.0;
            
            // Логируем проблемы с производительностью
            if (metrics_.error_rate > 0.05 || // 5% ошибок
                metrics_.throughput < 1.0) {   // минимум 1 задача в секунду
                std::stringstream ss;
                ss << "Performance issues detected:\n"
                   << "  Error rate: " << metrics_.error_rate << "%\n"
                   << "  Throughput: " << metrics_.throughput << " tasks/sec";
                logger_->log_diagnostic(config_.name, "performance", ss.str());
            }
        }
    }

    virtual void save_metrics_history() {
        metrics_history_[metrics_history_index_] = metrics_;
        metrics_history_index_ = (metrics_history_index_ + 1) % config_.metrics_history_size;
    }

    virtual void update_metrics(const std::string& operation, 
                              std::chrono::steady_clock::duration duration,
                              const std::string& error = "") {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        
        auto now = std::chrono::steady_clock::now();
        if (now - last_metrics_update_ >= config_.metrics_update_interval) {
            update_resource_metrics();
            update_performance_metrics();
            save_metrics_history();
            last_metrics_update_ = now;
            
            // Логируем обновленные метрики
            logger_->log_metrics(config_.name, metrics_);
        }
        
        metrics_.total_tasks++;
        
        if (operation == "completed") {
            metrics_.completed_tasks++;
        } else if (operation == "failed") {
            metrics_.failed_tasks++;
            if (!error.empty()) {
                metrics_.error_types[error]++;
            }
        } else if (operation == "cancelled") {
            metrics_.cancelled_tasks++;
        }

        auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration);
        metrics_.average_task_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            (metrics_.average_task_time * (metrics_.total_tasks - 1) + duration_ms) / 
            metrics_.total_tasks);
        
        metrics_.max_task_time = std::max(metrics_.max_task_time, duration_ms);
        metrics_.min_task_time = std::min(metrics_.min_task_time, duration_ms);
        metrics_.last_update = now;
    }
};

} // namespace kernels
} // namespace core
} // namespace cloud 