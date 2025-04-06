#include "core/kernels/kernel_manager.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace cloud {
namespace core {
namespace kernels {

KernelManager::KernelManager(const KernelConfig& config)
    : config_(config) {
    initialize_kernels();
}

KernelManager::~KernelManager() {
    stop();
}

void KernelManager::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!is_running_.load()) {
        try {
            high_priority_kernel_->start();
            medium_priority_kernel_->start();
            low_priority_kernel_->start();
            is_running_.store(true);
            // Запуск потоков мониторинга и масштабирования
            monitoring_thread_ = std::thread(&KernelManager::monitoring_loop, this);
            scaling_thread_ = std::thread(&KernelManager::scaling_loop, this);
            logger_->log_info("KernelManager", "Kernels started successfully.");
        } catch (const std::exception& e) {
            logger_->log_error("KernelManager", "Failed to start kernels: " + std::string(e.what()));
        }
    }
}

void KernelManager::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (is_running_.load()) {
        try {
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
            logger_->log_info("KernelManager", "Kernels stopped successfully.");
        } catch (const std::exception& e) {
            logger_->log_error("KernelManager", "Failed to stop kernels: " + std::string(e.what()));
        }
    }
}

void KernelManager::scale_kernels() {
    // Получаем текущие метрики
    auto current_metrics = get_metrics();

    // Определяем загрузки для каждого ядра
    struct KernelLoad {
        double load;
        size_t& num_threads;
        size_t max_threads;
        double scale_up_threshold;
        double scale_down_threshold;
    };

    // Инициализируем массив с конфигурациями для каждого ядра
    KernelLoad kernel_loads[] = {
        { static_cast<double>(current_metrics.high_priority.total_tasks) / current_metrics.high_priority.completed_tasks,
          config_.high_priority.num_threads, 4, 0.8, 0.2 },
        { static_cast<double>(current_metrics.medium_priority.total_tasks) / current_metrics.medium_priority.completed_tasks,
          config_.medium_priority.num_threads, 8, 0.8, 0.2 },
        { static_cast<double>(current_metrics.low_priority.total_tasks) / current_metrics.low_priority.completed_tasks,
          config_.low_priority.num_threads, 4, 0.8, 0.2 }
    };

    // Масштабируем ядра
    for (auto& kernel : kernel_loads) {
        if (kernel.load > kernel.scale_up_threshold) {
            kernel.num_threads = std::min(kernel.num_threads + 1, kernel.max_threads);
            logger_->log_info("KernelManager", "Increased threads for kernel to " + std::to_string(kernel.num_threads));
        } else if (kernel.load < kernel.scale_down_threshold) {
            kernel.num_threads = std::max(kernel.num_threads - 1, size_t(1));
            logger_->log_info("KernelManager", "Decreased threads for kernel to " + std::to_string(kernel.num_threads));
        }
    }
}

void KernelManager::balance_load() {
    // Получаем текущие метрики
    auto current_metrics = get_metrics();
    
    // Анализируем распределение задач
    size_t total_tasks = current_metrics.high_priority.total_tasks +
                        current_metrics.medium_priority.total_tasks +
                        current_metrics.low_priority.total_tasks;

    if (total_tasks == 0) {
        return;
    }

    // Вычисляем процентное соотношение задач
    double high_priority_ratio = static_cast<double>(current_metrics.high_priority.total_tasks) / total_tasks;
    double medium_priority_ratio = static_cast<double>(current_metrics.medium_priority.total_tasks) / total_tasks;
    double low_priority_ratio = static_cast<double>(current_metrics.low_priority.total_tasks) / total_tasks;

    // Целевые соотношения
    const double TARGET_HIGH_PRIORITY_RATIO = 0.2;    // 20% высокоприоритетных задач
    const double TARGET_MEDIUM_PRIORITY_RATIO = 0.5;  // 50% среднеприоритетных задач
    const double TARGET_LOW_PRIORITY_RATIO = 0.3;     // 30% низкоприоритетных задач

    // Определяем необходимость балансировки
    const double BALANCE_THRESHOLD = 0.1;  // 10% отклонение от целевого соотношения

    if (std::abs(high_priority_ratio - TARGET_HIGH_PRIORITY_RATIO) > BALANCE_THRESHOLD ||
        std::abs(medium_priority_ratio - TARGET_MEDIUM_PRIORITY_RATIO) > BALANCE_THRESHOLD ||
        std::abs(low_priority_ratio - TARGET_LOW_PRIORITY_RATIO) > BALANCE_THRESHOLD) {
        
        // Реализация балансировки нагрузки
        redistribute_tasks(current_metrics);
        adjust_task_priorities(current_metrics);
        redistribute_resources();
        logger_->log_info("KernelManager", "Load balanced successfully.");
    }
}

void KernelManager::redistribute_tasks(const KernelMetrics& metrics) {
    // Логика перемещения задач между ядрами
    // Перемещение задач из высокоприоритетного в средний приоритет
    if (metrics.high_priority.total_tasks > metrics.medium_priority.total_tasks) {
        size_t tasks_to_move = (metrics.high_priority.total_tasks - metrics.medium_priority.total_tasks) / 2;
        for (size_t i = 0; i < tasks_to_move; ++i) {
            move_task_to_medium_priority();
        }
        logger_->log_info("KernelManager", "Moved tasks from high to medium priority.");
    }

    // Перемещение задач из среднеприоритетного в низкий приоритет
    if (metrics.medium_priority.total_tasks > metrics.low_priority.total_tasks) {
        size_t tasks_to_move = (metrics.medium_priority.total_tasks - metrics.low_priority.total_tasks) / 2;
        for (size_t i = 0; i < tasks_to_move; ++i) {
            move_task_to_low_priority();
        }
        logger_->log_info("KernelManager", "Moved tasks from medium to low priority.");
    }
}

void KernelManager::adjust_task_priorities(const KernelMetrics& metrics) {
    // Логика изменения приоритетов задач
    // Повышение приоритета низкоприоритетных задач до среднего
    if (metrics.low_priority.total_tasks > metrics.medium_priority.total_tasks) {
        size_t tasks_to_promote = (metrics.low_priority.total_tasks - metrics.medium_priority.total_tasks) / 2;
        for (size_t i = 0; i < tasks_to_promote; ++i) {
            promote_task_to_medium_priority();
        }
        logger_->log_info("KernelManager", "Promoted low priority tasks to medium priority.");
    }

    // Понижение приоритета высокоприоритетных задач до среднего, если их слишком много
    if (metrics.high_priority.total_tasks > metrics.medium_priority.total_tasks) {
        size_t tasks_to_demote = (metrics.high_priority.total_tasks - metrics.medium_priority.total_tasks) / 2;
        for (size_t i = 0; i < tasks_to_demote; ++i) {
            demote_task_to_medium_priority();
        }
        logger_->log_info("KernelManager", "Demoted high priority tasks to medium priority.");
    }
}

void KernelManager::redistribute_resources() {
    // Логика перераспределения ресурсов между ядрами
    auto current_metrics = get_metrics();
    
    if (current_metrics.high_priority.total_tasks > current_metrics.medium_priority.total_tasks) {
        allocate_resources_to_medium_priority();
        logger_->log_info("KernelManager", "Resources redistributed from high to medium priority.");
    } else if (current_metrics.medium_priority.total_tasks > current_metrics.low_priority.total_tasks) {
        allocate_resources_to_low_priority();
        logger_->log_info("KernelManager", "Resources redistributed from medium to low priority.");
    }
}

// Примерные реализации вспомогательных функций
void KernelManager::move_task_to_medium_priority() {
    if (!high_priority_kernel_->task_queue.empty()) {
        auto task = high_priority_kernel_->task_queue.front(); // Получаем задачу
        high_priority_kernel_->task_queue.pop(); // Удаляем из высокоприоритетной очереди
        medium_priority_kernel_->task_queue.push(task); // Добавляем в среднеприоритетную очередь
    }
}

void KernelManager::move_task_to_low_priority() {
    if (!medium_priority_kernel_->task_queue.empty()) {
        auto task = medium_priority_kernel_->task_queue.front();
        medium_priority_kernel_->task_queue.pop();
        low_priority_kernel_->task_queue.push(task);
    }
}

void KernelManager::promote_task_to_medium_priority() {
    if (!low_priority_kernel_->task_queue.empty()) {
        auto task = low_priority_kernel_->task_queue.front();
        low_priority_kernel_->task_queue.pop();
        medium_priority_kernel_->task_queue.push(task);
    }
}

void KernelManager::demote_task_to_medium_priority() {
    if (!high_priority_kernel_->task_queue.empty()) {
        auto task = high_priority_kernel_->task_queue.front();
        high_priority_kernel_->task_queue.pop();
        medium_priority_kernel_->task_queue.push(task);
    }
}

void KernelManager::allocate_resources_to_medium_priority() {
    auto& medium_config = config_.medium_priority;
    auto& high_config = config_.high_priority;

    if (high_config.num_threads > 1) {
        high_config.num_threads--;
        medium_config.num_threads++;
    }
}

void KernelManager::allocate_resources_to_low_priority() {
    auto& low_config = config_.low_priority;
    auto& medium_config = config_.medium_priority;

    if (medium_config.num_threads > 2) {
        medium_config.num_threads--;
        low_config.num_threads++;
    }
}

} // namespace kernels
} // namespace core
} // namespace cloud 