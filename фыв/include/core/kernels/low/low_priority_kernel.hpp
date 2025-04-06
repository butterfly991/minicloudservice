#pragma once

#include "../base_kernel.hpp"
#include <queue>
#include <thread>
#include <chrono>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <unordered_map>
#include <algorithm>
#include <sys/resource.h>
#include <sys/sysinfo.h>
#include <unistd.h>

namespace cloud {
namespace core {
namespace kernels {
namespace low {

class LowPriorityKernel : public BaseKernel {
public:
    struct LowPriorityConfig : public BaseKernel::KernelConfig {
        // Настройки фоновых задач
        bool enable_background_processing{true};
        bool enable_idle_processing{true};
        std::chrono::seconds idle_check_interval{std::chrono::seconds(60)};
        
        // Настройки управления ресурсами
        bool enable_resource_throttling{true};
        double max_resource_usage{0.8};  // 80% ресурсов
        std::chrono::milliseconds resource_check_interval{std::chrono::seconds(5)};
        
        // Настройки очереди фоновых задач
        size_t max_background_tasks{1000};
        std::chrono::seconds background_task_timeout{std::chrono::seconds(300)};
        
        // Настройки оптимизации
        bool enable_task_batching{true};
        size_t batch_size{10};
        std::chrono::milliseconds batch_timeout{std::chrono::milliseconds(100)};
        
        // Настройки восстановления
        bool enable_auto_recovery{true};
        size_t max_retry_attempts{3};
        std::chrono::seconds retry_delay{std::chrono::seconds(5)};
    };

    LowPriorityKernel(std::shared_ptr<Logger> logger,
                      std::shared_ptr<ResourceManager> resourceManager,
                      std::shared_ptr<TaskManager> taskManager,
                      const LowPriorityConfig& config = LowPriorityConfig{})
        : BaseKernel(config), logger_(logger), resourceManager_(resourceManager), taskManager_(taskManager) {
        config_ = config;
        initialize_background_workers();
    }

    ~LowPriorityKernel() {
        stop_background_workers();
    }

    // Управление фоновыми задачами
    template<typename F, typename... Args>
    auto submit_background(F&& f, Args&&... args) 
        -> std::future<typename std::invoke_result<F, Args...>::type> {
        using return_type = typename std::invoke_result<F, Args...>::type;
        using task_type = std::packaged_task<return_type()>;

        auto task = std::make_shared<task_type>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<return_type> res = task->get_future();
        {
            std::lock_guard<std::mutex> lock(background_mutex_);
            if (background_tasks_.size() >= config_.max_background_tasks) {
                throw std::runtime_error("Background task queue is full");
            }
            background_tasks_.push_back({
                task,
                std::chrono::steady_clock::now() + config_.background_task_timeout
            });
        }
        background_condition_.notify_one();
        return res;
    }

    // Управление ресурсами
    void set_resource_limits(double max_cpu, size_t max_memory) {
        std::lock_guard<std::mutex> lock(resource_mutex_);
        resource_limits_.max_cpu = max_cpu;
        resource_limits_.max_memory = max_memory;
    }

    // Статистика
    struct BackgroundStats {
        size_t total_background_tasks{0};
        size_t completed_background_tasks{0};
        size_t failed_background_tasks{0};
        size_t timed_out_background_tasks{0};
        std::chrono::milliseconds average_background_task_time{0};
        std::unordered_map<std::string, size_t> background_task_types;
    };

    BackgroundStats get_background_stats() const {
        std::lock_guard<std::mutex> lock(background_mutex_);
        return background_stats_;
    }

    void process_background_tasks() {
        std::lock_guard<std::mutex> lock(background_mutex_);
        
        if (!config_.enable_background_processing) {
            return;
        }

        // Обработка фоновых задач
        while (background_running_) {
            if (background_tasks_.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            std::vector<BackgroundTask> tasks_to_process;
            while (!background_tasks_.empty() && tasks_to_process.size() < config_.batch_size) {
                tasks_to_process.push_back(background_tasks_.back());
                background_tasks_.pop_back();
            }

            for (const auto& task : tasks_to_process) {
                auto start_time = std::chrono::steady_clock::now();
                try {
                    (*task.task)();
                    auto end_time = std::chrono::steady_clock::now();
                    update_background_stats("completed", end_time - start_time);
                } catch (const std::exception& e) {
                    auto end_time = std::chrono::steady_clock::now();
                    update_background_stats("failed", end_time - start_time);
                    logger_->log_error(config_.name, "Background task failed: " + std::string(e.what()));
                }
            }
        }
    }

    void check_idle_tasks() {
        std::lock_guard<std::mutex> lock(background_mutex_);
        
        if (!config_.enable_idle_processing) {
            return;
        }

        auto now = std::chrono::steady_clock::now();
        auto idle_threshold = now - config_.idle_check_interval;

        // Проверяем простаивающие задачи
        for (auto it = background_tasks_.begin(); it != background_tasks_.end();) {
            // Здесь должна быть логика проверки времени ожидания задачи
            // и принятия решения о её обработке или отмене
            if (it->deadline < idle_threshold) {
                // Логика отмены или обработки простаивающей задачи
                logger_->log(KernelLogger::LogLevel::WARNING, config_.name,
                             "Idle task detected and will be removed.");
                it = background_tasks_.erase(it); // Удаляем задачу
            } else {
                ++it;
            }
        }
    }

    double get_current_cpu_usage() const {
        // Получаем статистику использования CPU
        struct rusage usage;
        if (getrusage(RUSAGE_SELF, &usage) != 0) {
            return 0.0;
        }

        // Конвертируем время CPU в проценты
        double user_time = usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1000000.0;
        double system_time = usage.ru_stime.tv_sec + usage.ru_stime.tv_usec / 1000000.0;
        
        // Получаем количество процессоров
        long num_processors = sysconf(_SC_NPROCESSORS_ONLN);
        if (num_processors <= 0) {
            return 0.0;
        }

        // Вычисляем процент использования CPU
        return (user_time + system_time) * 100.0 / num_processors;
    }

    size_t get_current_memory_usage() const {
        // Получаем информацию о памяти процесса
        struct rusage usage;
        if (getrusage(RUSAGE_SELF, &usage) != 0) {
            return 0;
        }

        // Возвращаем максимальное использование памяти в байтах
        return usage.ru_maxrss * 1024;  // Конвертируем из килобайт в байты
    }

    // Метод для динамического изменения приоритета задачи
    void changeTaskPriority(const std::string& task_id, int new_priority) {
        std::lock_guard<std::mutex> lock(background_mutex_);
        auto it = std::find_if(background_tasks_.begin(), background_tasks_.end(),
                               [&](const BackgroundTask& task) { return task.task->get_id() == task_id; });

        if (it != background_tasks_.end()) {
            // Изменяем приоритет и перемещаем задачу в очередь
            it->task->set_priority(new_priority);
            // Перемещаем задачу в нужное место в очереди
            BackgroundTask task = *it;
            background_tasks_.erase(it);
            // Вставляем задачу обратно в очередь в соответствии с её приоритетом
            auto insert_pos = std::find_if(background_tasks_.begin(), background_tasks_.end(),
                                            [&](const BackgroundTask& t) { return t.task->get_priority() < new_priority; });
            background_tasks_.insert(insert_pos, task);
            background_condition_.notify_one();
        } else {
            // Логирование ошибки, если задача не найдена
            logger_->log_error("LowPriorityKernel", "Task not found for priority change: " + task_id);
        }
    }

    void addTask(const std::string& task_id, std::function<void()> task_func) {
        // Implementation of addTask method
    }

protected:
    virtual void start() override {
        BaseKernel::start();
        start_background_workers();
    }

    virtual void stop() override {
        stop_background_workers();
        BaseKernel::stop();
    }

private:
    struct BackgroundTask {
        std::shared_ptr<std::packaged_task<void()>> task;
        std::chrono::steady_clock::time_point deadline;
    };

    struct ResourceLimits {
        double max_cpu{0.8};
        size_t max_memory{1024 * 1024 * 1024};  // 1GB
    };

    LowPriorityConfig config_;
    std::vector<BackgroundTask> background_tasks_;
    std::vector<std::thread> background_workers_;
    mutable std::mutex background_mutex_;
    std::condition_variable background_condition_;
    std::atomic<bool> background_running_{false};
    
    mutable std::mutex resource_mutex_;
    ResourceLimits resource_limits_;
    BackgroundStats background_stats_;
    
    std::shared_ptr<Logger> logger_;
    std::shared_ptr<ResourceManager> resourceManager_;
    std::shared_ptr<TaskManager> taskManager_;
    
    void initialize_background_workers() {
        background_workers_.reserve(2);  // Один для фоновых задач, один для проверки ресурсов
    }

    void start_background_workers() {
        background_running_.store(true, std::memory_order_release);
        
        // Запускаем обработчик фоновых задач
        background_workers_.emplace_back(&LowPriorityKernel::background_worker_thread, this);
        
        // Запускаем обработчик проверки ресурсов
        background_workers_.emplace_back(&LowPriorityKernel::resource_monitor_thread, this);
    }

    void stop_background_workers() {
        background_running_.store(false, std::memory_order_release);
        background_condition_.notify_all();
        
        for (auto& worker : background_workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        background_workers_.clear();
    }

    void background_worker_thread() {
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
                background_tasks_.pop_back();
            }

            auto start_time = std::chrono::steady_clock::now();
            try {
                task();
                auto end_time = std::chrono::steady_clock::now();
                update_background_stats("completed", end_time - start_time);
            } catch (const std::exception& e) {
                auto end_time = std::chrono::steady_clock::now();
                update_background_stats("failed", end_time - start_time);
                logger_->log_error(config_.name, "Background task failed: " + std::string(e.what()));
                recover_failed_tasks(); // Попытка восстановления
            }
            task_count_.fetch_sub(1, std::memory_order_release);
        }

        active_threads_.fetch_sub(1, std::memory_order_release);
        if (is_shutting_down_.load(std::memory_order_acquire)) {
            shutdown_condition_.notify_one();
        }
    }

    void resource_monitor_thread() {
        while (background_running_.load(std::memory_order_acquire)) {
            {
                std::lock_guard<std::mutex> lock(resource_mutex_);
                if (config_.enable_resource_throttling) {
                    throttle_resources();
                }
            }

            std::this_thread::sleep_for(config_.resource_check_interval);
        }
    }

    void throttle_resources() {
        std::lock_guard<std::mutex> lock(resource_mutex_);
        
        if (!config_.enable_resource_throttling) {
            return;
        }

        double current_cpu = get_current_cpu_usage();
        size_t current_memory = get_current_memory_usage();

        // Проверяем превышение лимитов
        if (current_cpu > resource_limits_.max_cpu || 
            current_memory > resource_limits_.max_memory) {
            apply_resource_limits(current_cpu, current_memory);
            
            // Логируем предупреждение
            std::stringstream ss;
            ss << "Resource throttling applied:\n"
               << "  CPU: " << current_cpu << "% (limit: " << resource_limits_.max_cpu << "%)\n"
               << "  Memory: " << current_memory << " bytes (limit: " 
               << resource_limits_.max_memory << " bytes)";
            logger_->log_diagnostic(config_.name, "resource_throttling", ss.str());
        }
    }

    void apply_resource_limits(double current_cpu, size_t current_memory) {
        // 1. Уменьшение количества активных потоков
        if (current_cpu > resource_limits_.max_cpu) {
            size_t current_threads = get_active_threads();
            if (current_threads > 1) {
                size_t new_thread_count = std::max(size_t(1), 
                    static_cast<size_t>(current_threads * (resource_limits_.max_cpu / current_cpu)));
                // TODO: Реализовать изменение количества потоков
            }
        }

        // 2. Приостановка некритичных задач
        if (current_memory > resource_limits_.max_memory) {
            std::lock_guard<std::mutex> lock(background_mutex_);
            // Приостанавливаем обработку новых фоновых задач
            config_.enable_background_processing = false;
            
            // Логируем действие
            logger_->log(KernelLogger::LogLevel::WARNING, config_.name,
                        "Background processing suspended due to high memory usage");
        }

        // 3. Очистка кэшей и освобождение памяти
        if (current_memory > resource_limits_.max_memory * 0.9) {  // 90% от лимита
            // TODO: Реализовать очистку кэшей
            // TODO: Реализовать освобождение неиспользуемой памяти
        }

        // 4. Управление очередью задач
        if (current_cpu > resource_limits_.max_cpu * 0.9 || 
            current_memory > resource_limits_.max_memory * 0.9) {
            std::lock_guard<std::mutex> lock(background_mutex_);
            // Уменьшаем размер очереди фоновых задач
            size_t new_queue_size = background_tasks_.size() / 2;
            while (background_tasks_.size() > new_queue_size) {
                background_tasks_.pop_back();
                update_background_stats("cancelled");
            }
            
            // Логируем действие
            logger_->log(KernelLogger::LogLevel::WARNING, config_.name,
                        "Background task queue reduced due to resource constraints");
        }
    }

    void update_background_stats(const std::string& operation,
                               std::chrono::steady_clock::duration duration = 
                               std::chrono::steady_clock::duration::zero()) {
        std::lock_guard<std::mutex> lock(background_mutex_);
        
        background_stats_.total_background_tasks++;
        
        if (operation == "completed") {
            background_stats_.completed_background_tasks++;
            auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration);
            background_stats_.average_background_task_time = 
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    (background_stats_.average_background_task_time * 
                     (background_stats_.completed_background_tasks - 1) + duration_ms) / 
                    background_stats_.completed_background_tasks);
        }
        else if (operation == "failed") {
            background_stats_.failed_background_tasks++;
        }
        else if (operation == "timed_out") {
            background_stats_.timed_out_background_tasks++;
        }
        else if (operation == "cancelled") {
            // Обработка отмененных задач
        }

        // Логирование состояния
        logger_->log_info("LowPriorityKernel", "Background task operation: " + operation);
    }

    void recover_failed_tasks() {
        std::lock_guard<std::mutex> lock(background_mutex_);
        
        for (auto it = background_tasks_.begin(); it != background_tasks_.end();) {
            if (it->task->is_failed()) {
                // Логика восстановления задачи
                logger_->log(KernelLogger::LogLevel::INFO, config_.name,
                             "Recovering failed task.");
                // Попробуем повторно выполнить задачу
                it->task->retry();
                it = background_tasks_.erase(it); // Удаляем задачу после попытки восстановления
            } else {
                ++it;
            }
        }
    }
};

} // namespace low
} // namespace kernels
} // namespace core
} // namespace cloud 