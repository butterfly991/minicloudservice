#include "core/kernel_manager.hpp"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <thread>
#include <chrono>
#include <stdexcept>

namespace cloud {

std::unique_ptr<KernelManager> KernelManager::instance_;
std::mutex KernelManager::instance_mutex_;

KernelManager& KernelManager::getInstance() {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    if (!instance_) {
        instance_ = std::make_unique<KernelManager>();
    }
    return *instance_;
}

KernelManager::KernelManager()
    : auto_recovery_enabled_(true)
    , load_balancing_enabled_(true)
    , monitoring_active_(false)
    , health_check_interval_(std::chrono::seconds(5)) {
    
    startMonitoring();
}

KernelManager::~KernelManager() {
    stopAll();
    monitoring_active_ = false;
    if (monitoring_thread_.joinable()) {
        monitoring_thread_.join();
    }
}

void KernelManager::registerKernel(const std::string& name, std::shared_ptr<KernelBase> kernel) {
    std::lock_guard<std::mutex> lock(kernels_mutex_);
    if (kernels_.find(name) != kernels_.end()) {
        throw std::runtime_error("Kernel " + name + " already registered");
    }
    
    kernels_[name] = kernel;
    initializeKernel(name);
}

void KernelManager::unregisterKernel(const std::string& name) {
    std::lock_guard<std::mutex> lock(kernels_mutex_);
    auto it = kernels_.find(name);
    if (it == kernels_.end()) {
        throw std::runtime_error("Kernel " + name + " not found");
    }
    
    cleanupKernel(name);
    kernels_.erase(it);
}

std::shared_ptr<KernelBase> KernelManager::getKernel(const std::string& name) const {
    std::lock_guard<std::mutex> lock(kernels_mutex_);
    auto it = kernels_.find(name);
    if (it == kernels_.end()) {
        throw std::runtime_error("Kernel " + name + " not found");
    }
    return it->second;
}

std::vector<std::string> KernelManager::getRegisteredKernels() const {
    std::lock_guard<std::mutex> lock(kernels_mutex_);
    std::vector<std::string> names;
    names.reserve(kernels_.size());
    for (const auto& pair : kernels_) {
        names.push_back(pair.first);
    }
    return names;
}

void KernelManager::setKernelConfig(const std::string& name, const KernelConfig& config) {
    std::lock_guard<std::mutex> lock(configs_mutex_);
    kernel_configs_[name] = config;
    
    auto kernel = getKernel(name);
    kernel->setMaxThreads(config.max_threads);
    kernel->setMaxQueueSize(config.max_queue_size);
    kernel->setTaskTimeout(config.task_timeout);
}

KernelManager::KernelConfig KernelManager::getKernelConfig(const std::string& name) const {
    std::lock_guard<std::mutex> lock(configs_mutex_);
    auto it = kernel_configs_.find(name);
    if (it == kernel_configs_.end()) {
        throw std::runtime_error("Config for kernel " + name + " not found");
    }
    return it->second;
}

void KernelManager::setAutoRecovery(bool enabled) {
    auto_recovery_enabled_ = enabled;
}

void KernelManager::setLoadBalancing(bool enabled) {
    load_balancing_enabled_ = enabled;
}

void KernelManager::startAll() {
    std::lock_guard<std::mutex> lock(kernels_mutex_);
    for (auto& pair : kernels_) {
        try {
            pair.second->start();
            updateKernelStatus(pair.first, true);
        } catch (const std::exception& e) {
            handleKernelError(pair.first, Error(Error::ErrorCode::KERNEL_ERROR,
                                              e.what(),
                                              Error::createContext("KernelManager", "startAll")));
        }
    }
}

void KernelManager::stopAll() {
    std::lock_guard<std::mutex> lock(kernels_mutex_);
    for (auto& pair : kernels_) {
        try {
            pair.second->stop();
            updateKernelStatus(pair.first, false);
        } catch (const std::exception& e) {
            handleKernelError(pair.first, Error(Error::ErrorCode::KERNEL_ERROR,
                                              e.what(),
                                              Error::createContext("KernelManager", "stopAll")));
        }
    }
}

void KernelManager::pauseAll() {
    std::lock_guard<std::mutex> lock(kernels_mutex_);
    for (auto& pair : kernels_) {
        try {
            pair.second->pause();
        } catch (const std::exception& e) {
            handleKernelError(pair.first, Error(Error::ErrorCode::KERNEL_ERROR,
                                              e.what(),
                                              Error::createContext("KernelManager", "pauseAll")));
        }
    }
}

void KernelManager::resumeAll() {
    std::lock_guard<std::mutex> lock(kernels_mutex_);
    for (auto& pair : kernels_) {
        try {
            pair.second->resume();
        } catch (const std::exception& e) {
            handleKernelError(pair.first, Error(Error::ErrorCode::KERNEL_ERROR,
                                              e.what(),
                                              Error::createContext("KernelManager", "resumeAll")));
        }
    }
}

void KernelManager::startKernel(const std::string& name) {
    auto kernel = getKernel(name);
    try {
        kernel->start();
        updateKernelStatus(name, true);
    } catch (const std::exception& e) {
        handleKernelError(name, Error(Error::ErrorCode::KERNEL_ERROR,
                                    e.what(),
                                    Error::createContext("KernelManager", "startKernel")));
    }
}

void KernelManager::stopKernel(const std::string& name) {
    auto kernel = getKernel(name);
    try {
        kernel->stop();
        updateKernelStatus(name, false);
    } catch (const std::exception& e) {
        handleKernelError(name, Error(Error::ErrorCode::KERNEL_ERROR,
                                    e.what(),
                                    Error::createContext("KernelManager", "stopKernel")));
    }
}

void KernelManager::pauseKernel(const std::string& name) {
    auto kernel = getKernel(name);
    try {
        kernel->pause();
    } catch (const std::exception& e) {
        handleKernelError(name, Error(Error::ErrorCode::KERNEL_ERROR,
                                    e.what(),
                                    Error::createContext("KernelManager", "pauseKernel")));
    }
}

void KernelManager::resumeKernel(const std::string& name) {
    auto kernel = getKernel(name);
    try {
        kernel->resume();
    } catch (const std::exception& e) {
        handleKernelError(name, Error(Error::ErrorCode::KERNEL_ERROR,
                                    e.what(),
                                    Error::createContext("KernelManager", "resumeKernel")));
    }
}

KernelManager::KernelStatus KernelManager::getKernelStatus(const std::string& name) const {
    std::lock_guard<std::mutex> lock(statuses_mutex_);
    auto it = kernel_statuses_.find(name);
    if (it == kernel_statuses_.end()) {
        throw std::runtime_error("Status for kernel " + name + " not found");
    }
    return it->second;
}

std::vector<KernelManager::KernelStatus> KernelManager::getAllKernelStatuses() const {
    std::lock_guard<std::mutex> lock(statuses_mutex_);
    std::vector<KernelStatus> statuses;
    statuses.reserve(kernel_statuses_.size());
    for (const auto& pair : kernel_statuses_) {
        statuses.push_back(pair.second);
    }
    return statuses;
}

bool KernelManager::isKernelHealthy(const std::string& name) const {
    return getKernelStatus(name).is_healthy;
}

double KernelManager::getSystemLoad() const {
    std::lock_guard<std::mutex> lock(statuses_mutex_);
    if (kernel_statuses_.empty()) {
        return 0.0;
    }
    
    double total_load = 0.0;
    for (const auto& pair : kernel_statuses_) {
        total_load += pair.second.current_load;
    }
    return total_load / kernel_statuses_.size();
}

void KernelManager::setErrorHandler(std::function<void(const Error&)> handler) {
    error_handler_ = std::move(handler);
}

void KernelManager::handleKernelError(const std::string& name, const Error& error) {
    if (error_handler_) {
        error_handler_(error);
    }
    
    if (auto_recovery_enabled_) {
        recoverKernel(name);
    }
}

void KernelManager::optimizeKernel(const std::string& name) {
    auto kernel = getKernel(name);
    auto& system_info = SystemInfo::getInstance();
    
    // Оптимизация на основе системной информации
    size_t optimal_threads = system_info.getOptimalThreadCount();
    size_t optimal_queue_size = system_info.getOptimalQueueSize();
    auto optimal_timeout = system_info.getOptimalTaskTimeout();
    
    kernel->setMaxThreads(optimal_threads);
    kernel->setMaxQueueSize(optimal_queue_size);
    kernel->setTaskTimeout(optimal_timeout);
    
    // Применяем флаги оптимизации
    if (system_info.hasAVX()) {
        kernel->setCompilationDirective(KernelFlags::CompilationDirective::USE_AVX);
    } else if (system_info.hasSSE()) {
        kernel->setCompilationDirective(KernelFlags::CompilationDirective::USE_SSE);
    }
    
    // Оптимизация под память
    if (system_info.getAvailableMemory() > 8 * 1024 * 1024 * 1024) { // 8GB
        kernel->setOptimizationFlag(KernelFlags::OptimizationFlag::MEMORY_OPTIMIZED);
    }
    
    // Оптимизация под сеть
    if (system_info.hasHighSpeedNetwork()) {
        kernel->setOptimizationFlag(KernelFlags::OptimizationFlag::NETWORK_OPTIMIZED);
    }
}

void KernelManager::optimizeAllKernels() {
    std::lock_guard<std::mutex> lock(kernels_mutex_);
    for (const auto& pair : kernels_) {
        optimizeKernel(pair.first);
    }
}

void KernelManager::setKernelOptimizationFlags(const std::string& name,
                                             const std::vector<KernelFlags::OptimizationFlag>& flags) {
    auto kernel = getKernel(name);
    for (const auto& flag : flags) {
        kernel->setOptimizationFlag(flag);
    }
}

void KernelManager::initializeKernel(const std::string& name) {
    auto kernel = getKernel(name);
    
    // Инициализация конфигурации по умолчанию
    KernelConfig default_config;
    default_config.name = name;
    default_config.max_threads = std::thread::hardware_concurrency();
    default_config.max_queue_size = 1000;
    default_config.task_timeout = std::chrono::seconds(30);
    default_config.auto_recovery = true;
    default_config.load_balancing = true;
    
    setKernelConfig(name, default_config);
    
    // Инициализация статуса
    KernelStatus status;
    status.name = name;
    status.is_running = false;
    status.is_healthy = true;
    status.restart_count = 0;
    status.current_load = 0.0;
    status.last_health_check = std::chrono::system_clock::now();
    
    std::lock_guard<std::mutex> lock(statuses_mutex_);
    kernel_statuses_[name] = status;
    
    // Оптимизация ядра
    optimizeKernel(name);
}

void KernelManager::cleanupKernel(const std::string& name) {
    std::lock_guard<std::mutex> lock(configs_mutex_);
    kernel_configs_.erase(name);
    
    std::lock_guard<std::mutex> status_lock(statuses_mutex_);
    kernel_statuses_.erase(name);
}

void KernelManager::monitorKernels() {
    while (monitoring_active_) {
        std::lock_guard<std::mutex> lock(kernels_mutex_);
        for (const auto& pair : kernels_) {
            checkKernelHealth(pair.first);
        }
        
        if (load_balancing_enabled_) {
            balanceLoad();
        }
        
        std::this_thread::sleep_for(health_check_interval_);
    }
}

void KernelManager::checkKernelHealth(const std::string& name) {
    auto kernel = getKernel(name);
    auto& status = kernel_statuses_[name];
    
    try {
        // Проверяем состояние ядра
        status.is_running = kernel->isRunning();
        status.current_load = kernel->getLoad();
        
        // Проверяем метрики
        auto metrics = kernel->getMetrics();
        if (metrics.messages_failed > metrics.messages_processed * 0.1) {
            status.is_healthy = false;
        } else {
            status.is_healthy = true;
        }
        
        status.last_health_check = std::chrono::system_clock::now();
        
        // Если ядро нездорово и включено авто-восстановление
        if (!status.is_healthy && auto_recovery_enabled_) {
            recoverKernel(name);
        }
    } catch (const std::exception& e) {
        status.is_healthy = false;
        handleKernelError(name, Error(Error::ErrorCode::KERNEL_ERROR,
                                    e.what(),
                                    Error::createContext("KernelManager", "checkKernelHealth")));
    }
}

void KernelManager::recoverKernel(const std::string& name) {
    auto& status = kernel_statuses_[name];
    if (status.restart_count >= 3) {
        handleKernelError(name, Error(Error::ErrorCode::KERNEL_ERROR,
                                    "Maximum restart attempts reached",
                                    Error::createContext("KernelManager", "recoverKernel")));
        return;
    }
    
    try {
        stopKernel(name);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        startKernel(name);
        status.restart_count++;
        status.is_healthy = true;
    } catch (const std::exception& e) {
        handleKernelError(name, Error(Error::ErrorCode::KERNEL_ERROR,
                                    e.what(),
                                    Error::createContext("KernelManager", "recoverKernel")));
    }
}

void KernelManager::balanceLoad() {
    std::vector<std::pair<std::string, double>> kernel_loads;
    
    // Собираем информацию о загрузке всех ядер
    for (const auto& pair : kernel_statuses_) {
        kernel_loads.emplace_back(pair.first, pair.second.current_load);
    }
    
    // Сортируем ядра по загрузке
    std::sort(kernel_loads.begin(), kernel_loads.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    
    // Если есть значительная разница в загрузке
    if (!kernel_loads.empty() && 
        kernel_loads.front().second - kernel_loads.back().second > 0.3) {
        // Перераспределяем задачи
        auto overloaded_kernel = getKernel(kernel_loads.front().first);
        auto underloaded_kernel = getKernel(kernel_loads.back().first);
        
        // TODO: Реализовать перераспределение задач между ядрами
    }
}

void KernelManager::updateMetrics() {
    std::lock_guard<std::mutex> lock(statuses_mutex_);
    for (auto& pair : kernel_statuses_) {
        try {
            auto kernel = getKernel(pair.first);
            pair.second.current_load = kernel->getLoad();
        } catch (const std::exception& e) {
            handleKernelError(pair.first, Error(Error::ErrorCode::KERNEL_ERROR,
                                              e.what(),
                                              Error::createContext("KernelManager", "updateMetrics")));
        }
    }
}

void KernelManager::startMonitoring() {
    monitoring_active_ = true;
    monitoring_thread_ = std::thread(&KernelManager::monitorKernels, this);
}

} // namespace cloud 