#include "core/kernel_flags.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <cpuid.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <unistd.h>

namespace cloud {

std::unique_ptr<KernelFlags> KernelFlags::instance_;
std::mutex KernelFlags::instance_mutex_;

KernelFlags& KernelFlags::getInstance() {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    if (!instance_) {
        instance_ = std::make_unique<KernelFlags>();
    }
    return *instance_;
}

KernelFlags::KernelFlags() {
    initializeFlags();
    detectSystemCapabilities();
    autoConfigure();
}

KernelFlags::~KernelFlags() = default;

void KernelFlags::initializeFlags() {
    config_.optimization_flags = 0;
    config_.compilation_directives = 0;
    config_.security_flags = 0;
    config_.debug_flags = 0;
}

void KernelFlags::detectSystemCapabilities() {
    detectCPUFeatures();
    detectMemoryFeatures();
    detectNetworkFeatures();
    detectSecurityFeatures();
}

void KernelFlags::detectCPUFeatures() {
    unsigned int eax, ebx, ecx, edx;
    
    // Проверка SSE
    __cpuid(1, eax, ebx, ecx, edx);
    capabilities_.has_sse = (edx & (1 << 26)) != 0;
    
    // Проверка AVX
    __cpuid(7, eax, ebx, ecx, edx);
    capabilities_.has_avx = (ebx & (1 << 5)) != 0;
    
    // Проверка NEON (ARM)
    #ifdef __ARM_NEON
    capabilities_.has_neon = true;
    #else
    capabilities_.has_neon = false;
    #endif
    
    // Определение максимального количества потоков
    capabilities_.max_threads = std::thread::hardware_concurrency();
}

void KernelFlags::detectMemoryFeatures() {
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        capabilities_.max_memory = si.totalram;
    }
    
    // Проверка поддержки больших страниц
    if (getauxval(AT_HWCAP) & HWCAP_2M) {
        capabilities_.supported_features.push_back("2MB_PAGES");
    }
}

void KernelFlags::detectNetworkFeatures() {
    // Проверка поддержки сетевых функций
    std::ifstream netdev("/proc/net/dev");
    if (netdev.is_open()) {
        capabilities_.supported_features.push_back("NETWORK_MONITORING");
    }
}

void KernelFlags::detectSecurityFeatures() {
    // Проверка поддержки шифрования
    capabilities_.has_encryption = true; // Предполагаем наличие OpenSSL
    
    // Проверка поддержки песочницы
    #ifdef __linux__
    capabilities_.has_sandbox = true;
    #else
    capabilities_.has_sandbox = false;
    #endif
}

void KernelFlags::autoConfigure() {
    // Настройка оптимизаций на основе возможностей системы
    if (capabilities_.has_sse) {
        setCompilationDirective(CompilationDirective::USE_SSE);
    }
    if (capabilities_.has_avx) {
        setCompilationDirective(CompilationDirective::USE_AVX);
    }
    if (capabilities_.has_neon) {
        setCompilationDirective(CompilationDirective::USE_NEON);
    }
    
    // Настройка режима работы
    if (capabilities_.max_threads >= 8) {
        setOptimizationFlag(OptimizationFlag::HIGH_PERFORMANCE);
    } else {
        setOptimizationFlag(OptimizationFlag::BALANCED);
    }
    
    // Настройка безопасности
    if (capabilities_.has_encryption) {
        setSecurityFlag(SecurityFlag::ENCRYPTION_ENABLED);
    }
    if (capabilities_.has_sandbox) {
        setSecurityFlag(SecurityFlag::SANDBOX_ENABLED);
    }
    
    // Включение базовых флагов отладки
    setDebugFlag(DebugFlag::PERFORMANCE_METRICS);
}

void KernelFlags::optimizeForSystem() {
    // Оптимизация под CPU
    if (hasOptimizationFlag(OptimizationFlag::CPU_OPTIMIZED)) {
        if (capabilities_.has_avx) {
            setCompilationDirective(CompilationDirective::USE_AVX);
        } else if (capabilities_.has_sse) {
            setCompilationDirective(CompilationDirective::USE_SSE);
        }
    }
    
    // Оптимизация под память
    if (hasOptimizationFlag(OptimizationFlag::MEMORY_OPTIMIZED)) {
        // Настройка параметров памяти на основе доступной памяти
        if (capabilities_.max_memory < 4 * 1024 * 1024 * 1024) { // 4GB
            setCustomFlag("MEMORY_POOL_SIZE", "2GB");
        } else {
            setCustomFlag("MEMORY_POOL_SIZE", "4GB");
        }
    }
    
    // Оптимизация под сеть
    if (hasOptimizationFlag(OptimizationFlag::NETWORK_OPTIMIZED)) {
        setCustomFlag("NETWORK_BUFFER_SIZE", "1MB");
        setCustomFlag("NETWORK_THREAD_COUNT", 
                     std::to_string(capabilities_.max_threads / 2));
    }
    
    // Оптимизация под диск
    if (hasOptimizationFlag(OptimizationFlag::DISK_OPTIMIZED)) {
        setCustomFlag("DISK_CACHE_SIZE", "256MB");
        setCustomFlag("DISK_IO_THREADS", 
                     std::to_string(capabilities_.max_threads / 4));
    }
    
    // Режим энергосбережения
    if (hasOptimizationFlag(OptimizationFlag::POWER_SAVING)) {
        setCustomFlag("CPU_FREQUENCY_LIMIT", "80%");
        setCustomFlag("MEMORY_USAGE_LIMIT", "70%");
    }
    
    validateConfiguration();
}

void KernelFlags::validateConfiguration() {
    // Проверка совместимости флагов
    if (hasOptimizationFlag(OptimizationFlag::HIGH_PERFORMANCE) &&
        hasOptimizationFlag(OptimizationFlag::POWER_SAVING)) {
        clearOptimizationFlag(OptimizationFlag::POWER_SAVING);
    }
    
    // Проверка директив компиляции
    if (hasCompilationDirective(CompilationDirective::USE_AVX) &&
        !capabilities_.has_avx) {
        clearCompilationDirective(CompilationDirective::USE_AVX);
    }
    
    if (hasCompilationDirective(CompilationDirective::USE_SSE) &&
        !capabilities_.has_sse) {
        clearCompilationDirective(CompilationDirective::USE_SSE);
    }
}

bool KernelFlags::isCompatibleWithSystem() const {
    return getCompatibilityIssues().empty();
}

std::vector<std::string> KernelFlags::getCompatibilityIssues() const {
    std::vector<std::string> issues;
    
    if (hasCompilationDirective(CompilationDirective::USE_AVX) &&
        !capabilities_.has_avx) {
        issues.push_back("AVX instructions not supported");
    }
    
    if (hasCompilationDirective(CompilationDirective::USE_SSE) &&
        !capabilities_.has_sse) {
        issues.push_back("SSE instructions not supported");
    }
    
    if (hasCompilationDirective(CompilationDirective::USE_NEON) &&
        !capabilities_.has_neon) {
        issues.push_back("NEON instructions not supported");
    }
    
    if (hasSecurityFlag(SecurityFlag::SANDBOX_ENABLED) &&
        !capabilities_.has_sandbox) {
        issues.push_back("Sandbox not supported on this system");
    }
    
    return issues;
}

// Управление флагами оптимизации
void KernelFlags::setOptimizationFlag(OptimizationFlag flag) {
    config_.optimization_flags |= static_cast<uint32_t>(flag);
}

void KernelFlags::clearOptimizationFlag(OptimizationFlag flag) {
    config_.optimization_flags &= ~static_cast<uint32_t>(flag);
}

bool KernelFlags::hasOptimizationFlag(OptimizationFlag flag) const {
    return (config_.optimization_flags & static_cast<uint32_t>(flag)) != 0;
}

// Управление директивами компиляции
void KernelFlags::setCompilationDirective(CompilationDirective directive) {
    config_.compilation_directives |= static_cast<uint32_t>(directive);
}

void KernelFlags::clearCompilationDirective(CompilationDirective directive) {
    config_.compilation_directives &= ~static_cast<uint32_t>(directive);
}

bool KernelFlags::hasCompilationDirective(CompilationDirective directive) const {
    return (config_.compilation_directives & static_cast<uint32_t>(directive)) != 0;
}

// Управление флагами безопасности
void KernelFlags::setSecurityFlag(SecurityFlag flag) {
    config_.security_flags |= static_cast<uint32_t>(flag);
}

void KernelFlags::clearSecurityFlag(SecurityFlag flag) {
    config_.security_flags &= ~static_cast<uint32_t>(flag);
}

bool KernelFlags::hasSecurityFlag(SecurityFlag flag) const {
    return (config_.security_flags & static_cast<uint32_t>(flag)) != 0;
}

// Управление флагами отладки
void KernelFlags::setDebugFlag(DebugFlag flag) {
    config_.debug_flags |= static_cast<uint32_t>(flag);
}

void KernelFlags::clearDebugFlag(DebugFlag flag) {
    config_.debug_flags &= ~static_cast<uint32_t>(flag);
}

bool KernelFlags::hasDebugFlag(DebugFlag flag) const {
    return (config_.debug_flags & static_cast<uint32_t>(flag)) != 0;
}

// Управление пользовательскими флагами
void KernelFlags::setCustomFlag(const std::string& name, const std::string& value) {
    config_.custom_flags[name] = value;
}

std::string KernelFlags::getCustomFlag(const std::string& name) const {
    auto it = config_.custom_flags.find(name);
    return it != config_.custom_flags.end() ? it->second : "";
}

void KernelFlags::clearCustomFlag(const std::string& name) {
    config_.custom_flags.erase(name);
}

// Получение и установка конфигурации
KernelFlags::KernelConfiguration KernelFlags::getConfiguration() const {
    return config_;
}

void KernelFlags::setConfiguration(const KernelConfiguration& config) {
    config_ = config;
    validateConfiguration();
}

} // namespace cloud 