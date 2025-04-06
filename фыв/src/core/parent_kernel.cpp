#include "core/parent_kernel.hpp"
#include <iostream>
#include <algorithm>
#include <stdexcept>
#include <sstream>
#include <thread>
#include <chrono>

namespace cloud {

ParentKernel::ParentKernel()
    : task_processing_active_(false)
    , monitoring_interval_(std::chrono::seconds(1))
    , health_check_interval_(std::chrono::seconds(5))
    , metrics_update_needed_(true) {
    
    // Инициализация менеджеров
    task_manager_ = std::make_unique<TaskManager>();
    resource_manager_ = std::make_unique<ResourceManager>();
    thread_pool_ = std::make_unique<ThreadPool>(std::thread::hardware_concurrency());
    
    // Инициализация метрик
    current_metrics_.total_cpu_usage = 0.0;
    current_metrics_.total_memory_usage = 0;
    current_metrics_.total_network_bandwidth = 0;
    current_metrics_.total_disk_io = 0;
}

ParentKernel::~ParentKernel() {
    stop();
}

void ParentKernel::start() {
    if (isRunning()) {
        return;
    }

    // Запускаем базовое ядро
    KernelBase::start();
    
    // Запускаем обработку задач
    task_processing_active_ = true;
    task_processing_thread_ = std::thread(&ParentKernel::processTaskQueue, this);
    
    // Запускаем мониторинг
    monitoring_thread_ = std::thread(&ParentKernel::monitorChildKernels, this);
    
    // Запускаем все дочерние ядра
    std::lock_guard<std::mutex> lock(child_kernels_mutex_);
    for (auto& pair : child_kernels_) {
        try {
            pair.second.kernel->start();
            pair.second.is_active = true;
            pair.second.last_heartbeat = std::chrono::system_clock::now();
        } catch (const std::exception& e) {
            handleChildKernelError(pair.first, Error(Error::ErrorCode::KERNEL_ERROR,
                                                   e.what(),
                                                   Error::createContext("ParentKernel", "start")));
        }
    }
}

void ParentKernel::stop() {
    if (!isRunning()) {
        return;
    }

    // Останавливаем обработку задач
    task_processing_active_ = false;
    task_queue_cv_.notify_all();
    if (task_processing_thread_.joinable()) {
        task_processing_thread_.join();
    }

    // Останавливаем мониторинг
    if (monitoring_thread_.joinable()) {
        monitoring_thread_.join();
    }

    // Останавливаем все дочерние ядра
    std::lock_guard<std::mutex> lock(child_kernels_mutex_);
    for (auto& pair : child_kernels_) {
        try {
            pair.second.kernel->stop();
            pair.second.is_active = false;
        } catch (const std::exception& e) {
            handleChildKernelError(pair.first, Error(Error::ErrorCode::KERNEL_ERROR,
                                                   e.what(),
                                                   Error::createContext("ParentKernel", "stop")));
        }
    }

    // Останавливаем базовое ядро
    KernelBase::stop();
}

void ParentKernel::pause() {
    if (!isRunning()) {
        return;
    }

    // Приостанавливаем обработку задач
    task_processing_active_ = false;
    task_queue_cv_.notify_all();

    // Приостанавливаем все дочерние ядра
    std::lock_guard<std::mutex> lock(child_kernels_mutex_);
    for (auto& pair : child_kernels_) {
        try {
            pair.second.kernel->pause();
        } catch (const std::exception& e) {
            handleChildKernelError(pair.first, Error(Error::ErrorCode::KERNEL_ERROR,
                                                   e.what(),
                                                   Error::createContext("ParentKernel", "pause")));
        }
    }

    // Приостанавливаем базовое ядро
    KernelBase::pause();
}

void ParentKernel::resume() {
    if (!isPaused()) {
        return;
    }

    // Возобновляем обработку задач
    task_processing_active_ = true;
    task_queue_cv_.notify_all();

    // Возобновляем все дочерние ядра
    std::lock_guard<std::mutex> lock(child_kernels_mutex_);
    for (auto& pair : child_kernels_) {
        try {
            pair.second.kernel->resume();
        } catch (const std::exception& e) {
            handleChildKernelError(pair.first, Error(Error::ErrorCode::KERNEL_ERROR,
                                                   e.what(),
                                                   Error::createContext("ParentKernel", "resume")));
        }
    }

    // Возобновляем базовое ядро
    KernelBase::resume();
}

void ParentKernel::registerChildKernel(const std::string& name, std::shared_ptr<KernelBase> kernel) {
    std::lock_guard<std::mutex> lock(child_kernels_mutex_);
    if (child_kernels_.find(name) != child_kernels_.end()) {
        throw std::runtime_error("Child kernel " + name + " already registered");
    }

    ChildKernelInfo info;
    info.kernel = kernel;
    info.type = kernel->getType();
    info.is_active = false;
    info.current_load = 0.0;
    info.task_count = 0;
    info.last_heartbeat = std::chrono::system_clock::now();
    
    // Определяем возможности ядра
    if (auto network_kernel = std::dynamic_pointer_cast<NetworkKernel>(kernel)) {
        info.capabilities.push_back("network");
    }
    if (auto blockchain_kernel = std::dynamic_pointer_cast<BlockchainKernel>(kernel)) {
        info.capabilities.push_back("blockchain");
    }

    child_kernels_[name] = info;
    initializeChildKernel(name);
}

void ParentKernel::unregisterChildKernel(const std::string& name) {
    std::lock_guard<std::mutex> lock(child_kernels_mutex_);
    auto it = child_kernels_.find(name);
    if (it == child_kernels_.end()) {
        throw std::runtime_error("Child kernel " + name + " not found");
    }

    cleanupChildKernel(name);
    child_kernels_.erase(it);
}

std::shared_ptr<KernelBase> ParentKernel::getChildKernel(const std::string& name) const {
    std::lock_guard<std::mutex> lock(child_kernels_mutex_);
    auto it = child_kernels_.find(name);
    if (it == child_kernels_.end()) {
        throw std::runtime_error("Child kernel " + name + " not found");
    }
    return it->second.kernel;
}

std::vector<std::string> ParentKernel::getChildKernels() const {
    std::lock_guard<std::mutex> lock(child_kernels_mutex_);
    std::vector<std::string> names;
    names.reserve(child_kernels_.size());
    for (const auto& pair : child_kernels_) {
        names.push_back(pair.first);
    }
    return names;
}

bool ParentKernel::isChildKernelActive(const std::string& name) const {
    std::lock_guard<std::mutex> lock(child_kernels_mutex_);
    auto it = child_kernels_.find(name);
    if (it == child_kernels_.end()) {
        return false;
    }
    return it->second.is_active;
}

void ParentKernel::setTaskDistribution(const std::string& kernel_name, const TaskDistribution& distribution) {
    std::lock_guard<std::mutex> lock(distributions_mutex_);
    task_distributions_[kernel_name] = distribution;
}

ParentKernel::TaskDistribution ParentKernel::getTaskDistribution(const std::string& kernel_name) const {
    std::lock_guard<std::mutex> lock(distributions_mutex_);
    auto it = task_distributions_.find(kernel_name);
    if (it == task_distributions_.end()) {
        throw std::runtime_error("Task distribution for kernel " + kernel_name + " not found");
    }
    return it->second;
}

void ParentKernel::updateTaskDistribution(const std::string& kernel_name, double new_load_factor) {
    std::lock_guard<std::mutex> lock(distributions_mutex_);
    auto it = task_distributions_.find(kernel_name);
    if (it != task_distributions_.end()) {
        it->second.load_factor = new_load_factor;
    }
}

void ParentKernel::balanceTasks() {
    std::vector<std::pair<std::string, double>> kernel_loads;
    
    // Собираем информацию о загрузке ядер
    std::lock_guard<std::mutex> lock(child_kernels_mutex_);
    for (const auto& pair : child_kernels_) {
        if (pair.second.is_active) {
            kernel_loads.emplace_back(pair.first, pair.second.current_load);
        }
    }
    
    // Сортируем ядра по загрузке
    std::sort(kernel_loads.begin(), kernel_loads.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    
    // Если есть значительная разница в загрузке
    if (!kernel_loads.empty() && 
        kernel_loads.front().second - kernel_loads.back().second > 0.3) {
        // Перераспределяем задачи
        auto overloaded_kernel = getChildKernel(kernel_loads.front().first);
        auto underloaded_kernel = getChildKernel(kernel_loads.back().first);
        
        // TODO: Реализовать перераспределение задач между ядрами
    }
}

void ParentKernel::setResourceQuota(const std::string& kernel_name, const ResourceQuota& quota) {
    std::lock_guard<std::mutex> lock(quotas_mutex_);
    resource_quotas_[kernel_name] = quota;
}

ParentKernel::ResourceQuota ParentKernel::getResourceQuota(const std::string& kernel_name) const {
    std::lock_guard<std::mutex> lock(quotas_mutex_);
    auto it = resource_quotas_.find(kernel_name);
    if (it == resource_quotas_.end()) {
        throw std::runtime_error("Resource quota for kernel " + kernel_name + " not found");
    }
    return it->second;
}

void ParentKernel::updateResourceQuota(const std::string& kernel_name, const ResourceQuota& new_quota) {
    std::lock_guard<std::mutex> lock(quotas_mutex_);
    resource_quotas_[kernel_name] = new_quota;
}

void ParentKernel::monitorResourceUsage() {
    std::lock_guard<std::mutex> lock(child_kernels_mutex_);
    for (const auto& pair : child_kernels_) {
        if (!pair.second.is_active) {
            continue;
        }

        try {
            auto kernel = pair.second.kernel;
            auto quota = getResourceQuota(pair.first);
            
            // Проверяем использование ресурсов
            if (kernel->getLoad() > 0.9) { // 90% загрузки CPU
                handleChildKernelError(pair.first, Error(Error::ErrorCode::RESOURCE_EXHAUSTED,
                                                       "CPU usage exceeded quota",
                                                       Error::createContext("ParentKernel", "monitorResourceUsage")));
            }
            
            // TODO: Добавить проверку других ресурсов (память, сеть, диск)
        } catch (const std::exception& e) {
            handleChildKernelError(pair.first, Error(Error::ErrorCode::KERNEL_ERROR,
                                                   e.what(),
                                                   Error::createContext("ParentKernel", "monitorResourceUsage")));
        }
    }
}

ParentKernel::SystemMetrics ParentKernel::getSystemMetrics() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    return current_metrics_;
}

void ParentKernel::updateMetrics() {
    if (!metrics_update_needed_) {
        return;
    }

    std::lock_guard<std::mutex> lock(metrics_mutex_);
    collectChildKernelMetrics();
    metrics_update_needed_ = false;
}

void ParentKernel::optimizeChildKernel(const std::string& name) {
    auto kernel = getChildKernel(name);
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

void ParentKernel::optimizeAllChildKernels() {
    std::lock_guard<std::mutex> lock(child_kernels_mutex_);
    for (const auto& pair : child_kernels_) {
        optimizeChildKernel(pair.first);
    }
}

void ParentKernel::setChildKernelOptimizationFlags(const std::string& name,
                                                 const std::vector<KernelFlags::OptimizationFlag>& flags) {
    auto kernel = getChildKernel(name);
    for (const auto& flag : flags) {
        kernel->setOptimizationFlag(flag);
    }
}

void ParentKernel::broadcastMessage(const std::string& message) {
    std::lock_guard<std::mutex> lock(child_kernels_mutex_);
    for (const auto& pair : child_kernels_) {
        if (pair.second.is_active) {
            try {
                pair.second.kernel->sendMessage("broadcast", message);
            } catch (const std::exception& e) {
                handleChildKernelError(pair.first, Error(Error::ErrorCode::KERNEL_ERROR,
                                                       e.what(),
                                                       Error::createContext("ParentKernel", "broadcastMessage")));
            }
        }
    }
}

void ParentKernel::sendMessage(const std::string& target, const std::string& message) {
    auto kernel = getChildKernel(target);
    try {
        kernel->sendMessage("direct", message);
    } catch (const std::exception& e) {
        handleChildKernelError(target, Error(Error::ErrorCode::KERNEL_ERROR,
                                           e.what(),
                                           Error::createContext("ParentKernel", "sendMessage")));
    }
}

void ParentKernel::registerMessageHandler(const std::string& type,
                                       std::function<void(const std::string&)> handler) {
    // Регистрируем обработчик в базовом классе
    KernelBase::registerMessageHandler(type, handler);
    
    // Регистрируем обработчик во всех дочерних ядрах
    std::lock_guard<std::mutex> lock(child_kernels_mutex_);
    for (const auto& pair : child_kernels_) {
        if (pair.second.is_active) {
            try {
                pair.second.kernel->registerMessageHandler(type, handler);
            } catch (const std::exception& e) {
                handleChildKernelError(pair.first, Error(Error::ErrorCode::KERNEL_ERROR,
                                                       e.what(),
                                                       Error::createContext("ParentKernel", "registerMessageHandler")));
            }
        }
    }
}

void ParentKernel::initializeChildKernel(const std::string& name) {
    auto kernel = getChildKernel(name);
    
    // Инициализация конфигурации по умолчанию
    ResourceQuota default_quota;
    default_quota.max_threads = std::thread::hardware_concurrency();
    default_quota.max_memory = 1024 * 1024 * 1024; // 1GB
    default_quota.max_network_bandwidth = 100 * 1024 * 1024; // 100MB/s
    default_quota.max_disk_io = 100 * 1024 * 1024; // 100MB/s
    default_quota.max_cpu_time = std::chrono::seconds(30);
    
    setResourceQuota(name, default_quota);
    
    // Инициализация распределения задач
    TaskDistribution default_distribution;
    default_distribution.kernel_name = name;
    default_distribution.load_factor = 1.0;
    default_distribution.priority = 1;
    
    setTaskDistribution(name, default_distribution);
    
    // Оптимизация ядра
    optimizeChildKernel(name);
}

void ParentKernel::cleanupChildKernel(const std::string& name) {
    std::lock_guard<std::mutex> lock(quotas_mutex_);
    resource_quotas_.erase(name);
    
    std::lock_guard<std::mutex> dist_lock(distributions_mutex_);
    task_distributions_.erase(name);
}

void ParentKernel::monitorChildKernels() {
    while (isRunning()) {
        std::lock_guard<std::mutex> lock(child_kernels_mutex_);
        for (const auto& pair : child_kernels_) {
            checkChildKernelHealth(pair.first);
        }
        
        monitorResourceUsage();
        updateMetrics();
        
        std::this_thread::sleep_for(monitoring_interval_);
    }
}

void ParentKernel::checkChildKernelHealth(const std::string& name) {
    auto& info = child_kernels_[name];
    auto kernel = info.kernel;
    
    try {
        // Проверяем состояние ядра
        info.is_active = kernel->isRunning();
        info.current_load = kernel->getLoad();
        
        // Проверяем метрики
        auto metrics = kernel->getMetrics();
        if (metrics.messages_failed > metrics.messages_processed * 0.1) {
            info.is_active = false;
        }
        
        info.last_heartbeat = std::chrono::system_clock::now();
        
        // Если ядро нездорово
        if (!info.is_active) {
            recoverChildKernel(name);
        }
    } catch (const std::exception& e) {
        info.is_active = false;
        handleChildKernelError(name, Error(Error::ErrorCode::KERNEL_ERROR,
                                         e.what(),
                                         Error::createContext("ParentKernel", "checkChildKernelHealth")));
    }
}

void ParentKernel::recoverChildKernel(const std::string& name) {
    auto& info = child_kernels_[name];
    
    try {
        // Останавливаем ядро
        info.kernel->stop();
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // Перезапускаем ядро
        info.kernel->start();
        info.is_active = true;
        info.last_heartbeat = std::chrono::system_clock::now();
        
        // Оптимизируем ядро
        optimizeChildKernel(name);
    } catch (const std::exception& e) {
        handleChildKernelError(name, Error(Error::ErrorCode::KERNEL_ERROR,
                                         e.what(),
                                         Error::createContext("ParentKernel", "recoverChildKernel")));
    }
}

void ParentKernel::processTaskQueue() {
    while (task_processing_active_) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(task_queue_mutex_);
            task_queue_cv_.wait(lock, [this] {
                return !task_queue_.empty() || !task_processing_active_;
            });
            
            if (!task_processing_active_) {
                break;
            }
            
            task = std::move(task_queue_.front());
            task_queue_.pop();
        }
        
        distributeTask(task);
    }
}

void ParentKernel::distributeTask(const Task& task) {
    std::vector<std::pair<std::string, double>> kernel_scores;
    
    // Вычисляем оценку для каждого ядра
    std::lock_guard<std::mutex> lock(child_kernels_mutex_);
    for (const auto& pair : child_kernels_) {
        if (!pair.second.is_active) {
            continue;
        }
        
        auto distribution = getTaskDistribution(pair.first);
        double score = distribution.load_factor * (1.0 - pair.second.current_load);
        
        // Учитываем приоритет
        score *= distribution.priority;
        
        // Проверяем соответствие возможностей
        bool has_capabilities = true;
        for (const auto& capability : distribution.required_capabilities) {
            if (std::find(pair.second.capabilities.begin(),
                         pair.second.capabilities.end(),
                         capability) == pair.second.capabilities.end()) {
                has_capabilities = false;
                break;
            }
        }
        
        if (has_capabilities) {
            kernel_scores.emplace_back(pair.first, score);
        }
    }
    
    // Выбираем лучшее ядро
    if (!kernel_scores.empty()) {
        auto best_kernel = std::max_element(kernel_scores.begin(), kernel_scores.end(),
                                          [](const auto& a, const auto& b) {
                                              return a.second < b.second;
                                          });
        
        try {
            auto kernel = getChildKernel(best_kernel->first);
            kernel->submitTask(task);
            
            // Обновляем статистику
            auto& info = child_kernels_[best_kernel->first];
            info.task_count++;
            info.current_load = kernel->getLoad();
        } catch (const std::exception& e) {
            handleChildKernelError(best_kernel->first, Error(Error::ErrorCode::KERNEL_ERROR,
                                                           e.what(),
                                                           Error::createContext("ParentKernel", "distributeTask")));
        }
    }
}

void ParentKernel::handleChildKernelError(const std::string& name, const Error& error) {
    // Логируем ошибку
    std::stringstream ss;
    ss << "Error in child kernel " << name << ": " << error.getMessage();
    ErrorHandler::handleError(Error(error.getCode(), ss.str(), error.getContext()));
    
    // Обновляем статус ядра
    updateChildKernelStatus(name, false);
}

void ParentKernel::updateChildKernelStatus(const std::string& name, bool is_active) {
    std::lock_guard<std::mutex> lock(child_kernels_mutex_);
    auto it = child_kernels_.find(name);
    if (it != child_kernels_.end()) {
        it->second.is_active = is_active;
        metrics_update_needed_ = true;
    }
}

void ParentKernel::collectChildKernelMetrics() {
    current_metrics_.total_cpu_usage = 0.0;
    current_metrics_.total_memory_usage = 0;
    current_metrics_.total_network_bandwidth = 0;
    current_metrics_.total_disk_io = 0;
    current_metrics_.kernel_loads.clear();
    current_metrics_.kernel_task_counts.clear();
    
    for (const auto& pair : child_kernels_) {
        if (!pair.second.is_active) {
            continue;
        }
        
        try {
            auto kernel = pair.second.kernel;
            auto metrics = kernel->getMetrics();
            
            current_metrics_.kernel_loads[pair.first] = pair.second.current_load;
            current_metrics_.kernel_task_counts[pair.first] = pair.second.task_count;
            
            // Обновляем общие метрики
            current_metrics_.total_cpu_usage += pair.second.current_load;
            current_metrics_.total_memory_usage += metrics.memory_usage;
            current_metrics_.total_network_bandwidth += metrics.network_bandwidth;
            current_metrics_.total_disk_io += metrics.disk_io;
        } catch (const std::exception& e) {
            handleChildKernelError(pair.first, Error(Error::ErrorCode::KERNEL_ERROR,
                                                   e.what(),
                                                   Error::createContext("ParentKernel", "collectChildKernelMetrics")));
        }
    }
    
    // Нормализуем общую загрузку CPU
    if (!child_kernels_.empty()) {
        current_metrics_.total_cpu_usage /= child_kernels_.size();
    }
}

} // namespace cloud 