#include "core/kernel_base.hpp"
#include <iostream>
#include <sstream>
#include <thread>
#include <chrono>

namespace cloud {

KernelBase::KernelBase()
    : running_(false)
    , paused_(false)
    , max_threads_(std::thread::hardware_concurrency())
    , max_queue_size_(1000)
    , task_timeout_(std::chrono::seconds(30))
    , adaptive_scaling_(true)
    , current_load_(0.0)
    , system_info_(SystemInfo::getInstance())
    , kernel_flags_(KernelFlags::getInstance()) {
    
    initializeFromSystemInfo();
    applyOptimizations();
}

KernelBase::~KernelBase() {
    stop();
    cleanupResources();
}

void KernelBase::initializeFromSystemInfo() {
    const auto& cpu_info = system_info_.getCPUInfo();
    const auto& memory_info = system_info_.getMemoryInfo();
    
    // Устанавливаем оптимальные параметры на основе системной информации
    max_threads_ = system_info_.getOptimalThreadCount();
    max_queue_size_ = system_info_.getOptimalQueueSize();
    task_timeout_ = system_info_.getOptimalTaskTimeout();
    
    // Инициализируем метрики
    metrics_.messages_processed = 0;
    metrics_.messages_failed = 0;
    metrics_.active_tasks = 0;
    metrics_.cpu_usage = 0.0;
    metrics_.memory_usage = 0.0;
    metrics_.last_update = std::chrono::system_clock::now();
}

void KernelBase::applyOptimizations() {
    // Применяем оптимизации на основе флагов
    if (hasOptimizationFlag(KernelFlags::OptimizationFlag::CPU_OPTIMIZED)) {
        if (hasCompilationDirective(KernelFlags::CompilationDirective::USE_AVX)) {
            // Применяем оптимизации AVX
            max_threads_ = std::min(max_threads_, 
                                  static_cast<size_t>(cpu_info_.logical_cores * 2));
        } else if (hasCompilationDirective(KernelFlags::CompilationDirective::USE_SSE)) {
            // Применяем оптимизации SSE
            max_threads_ = std::min(max_threads_, 
                                  static_cast<size_t>(cpu_info_.logical_cores * 1.5));
        }
    }
    
    if (hasOptimizationFlag(KernelFlags::OptimizationFlag::MEMORY_OPTIMIZED)) {
        // Оптимизация под память
        size_t memory_pool_size = std::stoul(kernel_flags_.getCustomFlag("MEMORY_POOL_SIZE"));
        max_queue_size_ = std::min(max_queue_size_, 
                                 static_cast<size_t>(memory_pool_size / 1024));
    }
    
    if (hasOptimizationFlag(KernelFlags::OptimizationFlag::NETWORK_OPTIMIZED)) {
        // Оптимизация под сеть
        size_t network_buffer_size = std::stoul(kernel_flags_.getCustomFlag("NETWORK_BUFFER_SIZE"));
        size_t network_threads = std::stoul(kernel_flags_.getCustomFlag("NETWORK_THREAD_COUNT"));
        max_threads_ = std::min(max_threads_, network_threads);
    }
    
    if (hasOptimizationFlag(KernelFlags::OptimizationFlag::DISK_OPTIMIZED)) {
        // Оптимизация под диск
        size_t disk_cache_size = std::stoul(kernel_flags_.getCustomFlag("DISK_CACHE_SIZE"));
        size_t disk_io_threads = std::stoul(kernel_flags_.getCustomFlag("DISK_IO_THREADS"));
        max_threads_ = std::min(max_threads_, disk_io_threads);
    }
    
    if (hasOptimizationFlag(KernelFlags::OptimizationFlag::POWER_SAVING)) {
        // Режим энергосбережения
        std::string cpu_limit = kernel_flags_.getCustomFlag("CPU_FREQUENCY_LIMIT");
        std::string memory_limit = kernel_flags_.getCustomFlag("MEMORY_USAGE_LIMIT");
        
        if (!cpu_limit.empty()) {
            double limit = std::stod(cpu_limit.substr(0, cpu_limit.length() - 1)) / 100.0;
            max_threads_ = static_cast<size_t>(max_threads_ * limit);
        }
        
        if (!memory_limit.empty()) {
            double limit = std::stod(memory_limit.substr(0, memory_limit.length() - 1)) / 100.0;
            max_queue_size_ = static_cast<size_t>(max_queue_size_ * limit);
        }
    }
    
    validateConfiguration();
}

void KernelBase::validateConfiguration() {
    // Проверяем совместимость с системой
    if (!kernel_flags_.isCompatibleWithSystem()) {
        auto issues = kernel_flags_.getCompatibilityIssues();
        for (const auto& issue : issues) {
            handleError(Error(Error::ErrorCode::KERNEL_ERROR,
                            issue,
                            Error::createContext("KernelBase", "validateConfiguration")));
        }
    }
    
    // Проверяем безопасность
    if (hasSecurityFlag(KernelFlags::SecurityFlag::ENCRYPTION_ENABLED)) {
        // Включаем шифрование
    }
    
    if (hasSecurityFlag(KernelFlags::SecurityFlag::SANDBOX_ENABLED)) {
        // Включаем песочницу
    }
    
    // Проверяем отладку
    if (hasDebugFlag(KernelFlags::DebugFlag::VERBOSE_LOGGING)) {
        // Включаем подробное логирование
    }
    
    if (hasDebugFlag(KernelFlags::DebugFlag::PERFORMANCE_METRICS)) {
        // Включаем сбор метрик производительности
    }
}

void KernelBase::start() {
    if (running_) {
        return;
    }

    try {
        running_ = true;
        paused_ = false;
        updateMetrics();
    } catch (const std::exception& e) {
        handleError(Error(Error::ErrorCode::KERNEL_ERROR,
                         e.what(),
                         Error::createContext("KernelBase", "start")));
        running_ = false;
        throw;
    }
}

void KernelBase::stop() {
    if (!running_) {
        return;
    }

    try {
        running_ = false;
        paused_ = false;
        cleanupResources();
    } catch (const std::exception& e) {
        handleError(Error(Error::ErrorCode::KERNEL_ERROR,
                         e.what(),
                         Error::createContext("KernelBase", "stop")));
        throw;
    }
}

void KernelBase::pause() {
    if (!running_ || paused_) {
        return;
    }

    try {
        paused_ = true;
        updateMetrics();
    } catch (const std::exception& e) {
        handleError(Error(Error::ErrorCode::KERNEL_ERROR,
                         e.what(),
                         Error::createContext("KernelBase", "pause")));
        throw;
    }
}

void KernelBase::resume() {
    if (!running_ || !paused_) {
        return;
    }

    try {
        paused_ = false;
        updateMetrics();
    } catch (const std::exception& e) {
        handleError(Error(Error::ErrorCode::KERNEL_ERROR,
                         e.what(),
                         Error::createContext("KernelBase", "resume")));
        throw;
    }
}

bool KernelBase::isRunning() const {
    return running_;
}

bool KernelBase::isPaused() const {
    return paused_;
}

void KernelBase::sendMessage(const std::string& message) {
    if (!running_ || paused_) {
        return;
    }

    try {
        processMessage(message);
        metrics_.messages_processed++;
        updateMetrics();
    } catch (const std::exception& e) {
        metrics_.messages_failed++;
        handleError(Error(Error::ErrorCode::KERNEL_ERROR,
                         e.what(),
                         Error::createContext("KernelBase", "sendMessage")));
    }
}

void KernelBase::broadcastMessage(const std::string& message) {
    if (!running_ || paused_) {
        return;
    }

    try {
        // Реализация broadcast будет зависеть от конкретного ядра
        metrics_.messages_processed++;
        updateMetrics();
    } catch (const std::exception& e) {
        metrics_.messages_failed++;
        handleError(Error(Error::ErrorCode::KERNEL_ERROR,
                         e.what(),
                         Error::createContext("KernelBase", "broadcastMessage")));
    }
}

void KernelBase::registerMessageHandler(const std::string& type,
                                      std::function<void(const std::string&)> handler) {
    message_handlers_[type] = std::move(handler);
}

void KernelBase::setMaxThreads(size_t count) {
    max_threads_ = count;
    if (adaptive_scaling_) {
        updateScalingParameters();
    }
}

void KernelBase::setMaxQueueSize(size_t size) {
    max_queue_size_ = size;
    if (adaptive_scaling_) {
        updateScalingParameters();
    }
}

void KernelBase::setTaskTimeout(std::chrono::milliseconds timeout) {
    task_timeout_ = timeout;
    if (adaptive_scaling_) {
        updateScalingParameters();
    }
}

size_t KernelBase::getMaxThreads() const {
    return max_threads_;
}

size_t KernelBase::getMaxQueueSize() const {
    return max_queue_size_;
}

std::chrono::milliseconds KernelBase::getTaskTimeout() const {
    return task_timeout_;
}

KernelBase::KernelMetrics KernelBase::getMetrics() const {
    return metrics_;
}

double KernelBase::getLoad() const {
    return current_load_;
}

void KernelBase::enableAdaptiveScaling() {
    adaptive_scaling_ = true;
    updateScalingParameters();
}

void KernelBase::disableAdaptiveScaling() {
    adaptive_scaling_ = false;
}

bool KernelBase::isAdaptiveScalingEnabled() const {
    return adaptive_scaling_;
}

void KernelBase::updateMetrics() {
    auto now = std::chrono::system_clock::now();
    if (now - metrics_.last_update > std::chrono::seconds(1)) {
        updateResourceUsage();
        metrics_.last_update = now;
    }
}

void KernelBase::handleError(const Error& error) {
    ErrorHandler::handleError(error);
}

void KernelBase::processMessage(const std::string& message) {
    // Базовая реализация - просто передаем сообщение всем зарегистрированным обработчикам
    for (const auto& handler : message_handlers_) {
        try {
            handler.second(message);
        } catch (const std::exception& e) {
            handleError(Error(Error::ErrorCode::KERNEL_ERROR,
                            e.what(),
                            Error::createContext("KernelBase", "processMessage")));
        }
    }
}

void KernelBase::updateResourceUsage() {
    // Обновляем метрики на основе системной информации
    metrics_.cpu_usage = system_info_.getCPUUsage();
    metrics_.memory_usage = system_info_.getMemoryUsage();
    
    // Рассчитываем текущую загрузку
    current_load_ = (metrics_.cpu_usage + metrics_.memory_usage) / 2.0;
    
    // Если включена адаптивная настройка, обновляем параметры
    if (adaptive_scaling_) {
        updateScalingParameters();
    }
}

void KernelBase::updateScalingParameters() {
    if (!adaptive_scaling_) {
        return;
    }

    // Получаем текущие системные метрики
    double cpu_usage = system_info_.getCPUUsage();
    double memory_usage = system_info_.getMemoryUsage();
    double system_load = system_info_.getSystemLoad();

    // Адаптируем количество потоков
    if (cpu_usage > 80.0) {
        // Уменьшаем количество потоков при высокой загрузке CPU
        size_t new_thread_count = max_threads_ / 2;
        max_threads_ = std::max(new_thread_count, 
                              static_cast<size_t>(std::thread::hardware_concurrency() / 4));
    } else if (cpu_usage < 30.0) {
        // Увеличиваем количество потоков при низкой загрузке
        size_t new_thread_count = max_threads_ * 2;
        max_threads_ = std::min(new_thread_count, 
                              static_cast<size_t>(std::thread::hardware_concurrency() * 2));
    }

    // Адаптируем размер очереди
    if (system_load > max_threads_ * 0.8) {
        // Уменьшаем размер очереди при высокой нагрузке
        max_queue_size_ = max_queue_size_ / 2;
    } else if (system_load < max_threads_ * 0.3) {
        // Увеличиваем размер очереди при низкой нагрузке
        max_queue_size_ = max_queue_size_ * 2;
    }

    // Адаптируем таймаут задач
    if (memory_usage > 80.0) {
        // Уменьшаем таймаут при высокой загрузке памяти
        task_timeout_ = std::chrono::milliseconds(500);
    } else if (memory_usage < 30.0) {
        // Увеличиваем таймаут при низкой загрузке памяти
        task_timeout_ = std::chrono::seconds(5);
    }
}

void KernelBase::cleanupResources() {
    message_handlers_.clear();
    metrics_ = KernelMetrics();
    current_load_ = 0.0;
}

// Добавляем новые методы для управления флагами
void KernelBase::setOptimizationFlag(KernelFlags::OptimizationFlag flag) {
    kernel_flags_.setOptimizationFlag(flag);
    applyOptimizations();
}

void KernelBase::clearOptimizationFlag(KernelFlags::OptimizationFlag flag) {
    kernel_flags_.clearOptimizationFlag(flag);
    applyOptimizations();
}

bool KernelBase::hasOptimizationFlag(KernelFlags::OptimizationFlag flag) const {
    return kernel_flags_.hasOptimizationFlag(flag);
}

void KernelBase::setCompilationDirective(KernelFlags::CompilationDirective directive) {
    kernel_flags_.setCompilationDirective(directive);
    applyOptimizations();
}

void KernelBase::clearCompilationDirective(KernelFlags::CompilationDirective directive) {
    kernel_flags_.clearCompilationDirective(directive);
    applyOptimizations();
}

bool KernelBase::hasCompilationDirective(KernelFlags::CompilationDirective directive) const {
    return kernel_flags_.hasCompilationDirective(directive);
}

void KernelBase::setSecurityFlag(KernelFlags::SecurityFlag flag) {
    kernel_flags_.setSecurityFlag(flag);
    validateConfiguration();
}

void KernelBase::clearSecurityFlag(KernelFlags::SecurityFlag flag) {
    kernel_flags_.clearSecurityFlag(flag);
    validateConfiguration();
}

bool KernelBase::hasSecurityFlag(KernelFlags::SecurityFlag flag) const {
    return kernel_flags_.hasSecurityFlag(flag);
}

void KernelBase::setDebugFlag(KernelFlags::DebugFlag flag) {
    kernel_flags_.setDebugFlag(flag);
    validateConfiguration();
}

void KernelBase::clearDebugFlag(KernelFlags::DebugFlag flag) {
    kernel_flags_.clearDebugFlag(flag);
    validateConfiguration();
}

bool KernelBase::hasDebugFlag(KernelFlags::DebugFlag flag) const {
    return kernel_flags_.hasDebugFlag(flag);
}

} // namespace cloud 