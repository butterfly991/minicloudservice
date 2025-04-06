#include "resource_manager.hpp"
#include <sys/resource.h>
#include <sys/sysinfo.h>
#include <unistd.h>
#include <iostream>

namespace cloud {
namespace core {
namespace kernels {
namespace low {

ResourceManager::ResourceManager(double max_cpu, size_t max_memory)
    : max_cpu_(max_cpu), max_memory_(max_memory) {}

void ResourceManager::check_resources() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    double current_cpu = get_current_cpu_usage();
    size_t current_memory = get_current_memory_usage();

    // Логируем текущее использование ресурсов
    std::cout << "Current CPU usage: " << current_cpu << "%, Current Memory usage: " << current_memory << " bytes." << std::endl;
}

void ResourceManager::apply_limits(double current_cpu, size_t current_memory) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Применяем ограничения на основе текущего использования ресурсов
    if (current_cpu > max_cpu_) {
        std::cout << "CPU usage exceeded limit. Current: " << current_cpu << "%, Limit: " << max_cpu_ << "%" << std::endl;
        // Логика для уменьшения нагрузки, например, уменьшение количества потоков
    }

    if (current_memory > max_memory_) {
        std::cout << "Memory usage exceeded limit. Current: " << current_memory << " bytes, Limit: " << max_memory_ << " bytes." << std::endl;
        // Логика для освобождения памяти, например, приостановка фоновых задач
    }
}

void ResourceManager::set_limits(double max_cpu, size_t max_memory) {
    std::lock_guard<std::mutex> lock(mutex_);
    max_cpu_ = max_cpu;
    max_memory_ = max_memory;
    std::cout << "Resource limits updated: Max CPU = " << max_cpu_ 
              << ", Max Memory = " << max_memory_ << " bytes." << std::endl;
}

double ResourceManager::get_max_cpu() const {
    return max_cpu_;
}

size_t ResourceManager::get_max_memory() const {
    return max_memory_;
}

} // namespace low
} // namespace kernels
} // namespace core
} // namespace cloud
