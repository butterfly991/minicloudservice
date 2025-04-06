#pragma once

#include "../base_kernel.hpp"

namespace cloud {
namespace core {
namespace kernels {
namespace high {

class HighPriorityKernel : public BaseKernel {
public:
    struct HighPriorityConfig : public BaseKernel::KernelConfig {
        size_t max_concurrent_tasks{100};
        std::chrono::milliseconds task_timeout{std::chrono::seconds(5)};
        bool preempt_lower_priority{true};
        bool enable_priority_boost{true};
        std::chrono::milliseconds priority_boost_duration{std::chrono::seconds(30)};
    };

    struct Task {
        std::string id; // Идентификатор задачи
        std::function<void()> func; // Функция задачи
        int priority; // Приоритет задачи

        Task(const std::string& task_id, std::function<void()> task_func, int task_priority = 0)
            : id(task_id), func(std::move(task_func)), priority(task_priority) {}
    };

    HighPriorityKernel(std::shared_ptr<Logger> logger,
                       std::shared_ptr<ResourceManager> resourceManager,
                       std::shared_ptr<TaskManager> taskManager)
        : logger_(logger), resourceManager_(resourceManager), taskManager_(taskManager) {
        config_.priority = 100;
        config_.max_tasks = 1000;
        config_.num_threads = 2;
        config_.enable_metrics = true;
        config_.enable_tracing = true;
        config_.name = "HighPriorityKernel";
    }

    // Специфичные для высокого приоритета методы
    virtual void boost_priority(const std::string& task_id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Находим задачу по ID
        auto task_it = std::find_if(tasks_.begin(), tasks_.end(),
            [&task_id](const auto& task) {
                return task.id == task_id; // Сравнение ID задачи
            });

        if (task_it != tasks_.end()) {
            // Повышаем приоритет задачи
            Task task = *task_it;
            tasks_.erase(task_it); // Удаляем задачу из текущей позиции
            tasks_.push_front(task); // Перемещаем задачу в начало очереди
            condition_.notify_one(); // Уведомляем рабочие потоки
        } else {
            // Логирование ошибки, если задача не найдена
            // logger_->log_error("HighPriorityKernel", "Task not found: " + task_id);
        }
    }

    virtual void preempt_task(const std::string& task_id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Находим задачу по ID
        auto task_it = std::find_if(tasks_.begin(), tasks_.end(),
            [&task_id](const auto& task) {
                return task.id == task_id; // Сравнение ID задачи
            });

        if (task_it != tasks_.end()) {
            // Вытесняем задачу
            tasks_.erase(task_it); // Удаляем задачу из очереди
            // Уведомляем о вытеснении
            // logger_->log_info("HighPriorityKernel", "Task preempted: " + task_id);
        } else {
            // Логирование ошибки, если задача не найдена
            // logger_->log_error("HighPriorityKernel", "Task not found for preemption: " + task_id);
        }
    }

    // Переопределение базовых методов для специфичной логики
    virtual void start() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!is_running_.load(std::memory_order_acquire)) {
            // Дополнительная инициализация для высокого приоритета
            is_running_.store(true, std::memory_order_release);
            is_paused_.store(false, std::memory_order_release);
            is_shutting_down_.store(false, std::memory_order_release);
            condition_.notify_all();
        }
    }

    // Добавление задачи
    void addTask(const std::string& task_id, std::function<void()> task_func) {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.emplace_back(Task{task_id, std::move(task_func)});
        condition_.notify_one(); // Уведомляем рабочие потоки
    }

    // Удаление задачи
    bool removeTask(const std::string& task_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::remove_if(tasks_.begin(), tasks_.end(), [&](const Task& task) {
            return task.id == task_id;
        });

        if (it != tasks_.end()) {
            tasks_.erase(it, tasks_.end());
            return true; // Задача успешно удалена
        }
        return false; // Задача не найдена
    }

    // Получение количества задач
    size_t getTaskCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return tasks_.size();
    }

    // Установка приоритета задачи
    void setTaskPriority(const std::string& task_id, int priority) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::find_if(tasks_.begin(), tasks_.end(), [&](const Task& task) {
            return task.id == task_id; // Поиск по идентификатору
        });

        if (it != tasks_.end()) {
            it->priority = priority; // Устанавливаем новый приоритет

            // Перемещаем задачу в нужное место в очереди в зависимости от нового приоритета
            Task task = *it;
            tasks_.erase(it);

            // Вставляем задачу обратно в очередь в соответствии с её приоритетом
            auto insert_pos = std::find_if(tasks_.begin(), tasks_.end(), [&](const Task& t) {
                return t.priority < task.priority; // Находим позицию для вставки
            });

            tasks_.insert(insert_pos, task); // Вставляем задачу в нужное место
            condition_.notify_one(); // Уведомляем рабочие потоки
        } else {
            // Логирование ошибки, если задача не найдена
            // logger_->log_error("HighPriorityKernel", "Task not found for priority update: " + task_id);
        }
    }

    // Получение метрик ядра
    KernelMetrics getMetrics() const {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        return metrics_; // Возвращаем текущие метрики
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
                task(); // Выполняем задачу
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
    HighPriorityConfig high_priority_config_;
    std::shared_ptr<Logger> logger_;
    std::shared_ptr<ResourceManager> resourceManager_;
    std::shared_ptr<TaskManager> taskManager_;
};

} // namespace high
} // namespace kernels
} // namespace core
} // namespace cloud 