#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <cstdint>

namespace cloud {

class KernelFlags {
public:
    // Флаги оптимизации
    enum class OptimizationFlag {
        NONE = 0,
        CPU_OPTIMIZED = 1 << 0,      // Оптимизация под CPU
        MEMORY_OPTIMIZED = 1 << 1,   // Оптимизация под память
        NETWORK_OPTIMIZED = 1 << 2,  // Оптимизация под сеть
        DISK_OPTIMIZED = 1 << 3,     // Оптимизация под диск
        POWER_SAVING = 1 << 4,       // Режим энергосбережения
        HIGH_PERFORMANCE = 1 << 5,   // Режим высокой производительности
        BALANCED = 1 << 6,          // Сбалансированный режим
        ALL = 0x7F                   // Все флаги
    };

    // Директивы компиляции
    enum class CompilationDirective {
        NONE = 0,
        USE_SSE = 1 << 0,           // Использовать SSE инструкции
        USE_AVX = 1 << 1,           // Использовать AVX инструкции
        USE_NEON = 1 << 2,          // Использовать NEON инструкции (ARM)
        USE_GPU = 1 << 3,           // Использовать GPU
        USE_TPU = 1 << 4,           // Использовать TPU
        USE_QUANTIZATION = 1 << 5,  // Использовать квантование
        ALL = 0x3F                   // Все директивы
    };

    // Флаги безопасности
    enum class SecurityFlag {
        NONE = 0,
        ENCRYPTION_ENABLED = 1 << 0,
        SANDBOX_ENABLED = 1 << 1,
        MEMORY_PROTECTION = 1 << 2,
        NETWORK_SECURITY = 1 << 3,
        ALL = 0x0F
    };

    // Флаги отладки
    enum class DebugFlag {
        NONE = 0,
        VERBOSE_LOGGING = 1 << 0,
        PERFORMANCE_METRICS = 1 << 1,
        MEMORY_TRACKING = 1 << 2,
        NETWORK_MONITORING = 1 << 3,
        ALL = 0x0F
    };

    struct KernelConfiguration {
        uint32_t optimization_flags;
        uint32_t compilation_directives;
        uint32_t security_flags;
        uint32_t debug_flags;
        std::unordered_map<std::string, std::string> custom_flags;
    };

    static KernelFlags& getInstance();

    // Управление флагами
    void setOptimizationFlag(OptimizationFlag flag);
    void clearOptimizationFlag(OptimizationFlag flag);
    bool hasOptimizationFlag(OptimizationFlag flag) const;

    void setCompilationDirective(CompilationDirective directive);
    void clearCompilationDirective(CompilationDirective directive);
    bool hasCompilationDirective(CompilationDirective directive) const;

    void setSecurityFlag(SecurityFlag flag);
    void clearSecurityFlag(SecurityFlag flag);
    bool hasSecurityFlag(SecurityFlag flag) const;

    void setDebugFlag(DebugFlag flag);
    void clearDebugFlag(DebugFlag flag);
    bool hasDebugFlag(DebugFlag flag) const;

    // Управление конфигурацией
    void setCustomFlag(const std::string& name, const std::string& value);
    std::string getCustomFlag(const std::string& name) const;
    void clearCustomFlag(const std::string& name);

    // Получение текущей конфигурации
    KernelConfiguration getConfiguration() const;
    void setConfiguration(const KernelConfiguration& config);

    // Автоматическая настройка
    void autoConfigure();
    void detectSystemCapabilities();
    void optimizeForSystem();

    // Проверка совместимости
    bool isCompatibleWithSystem() const;
    std::vector<std::string> getCompatibilityIssues() const;

private:
    KernelFlags();
    ~KernelFlags();

    // Запрет копирования
    KernelFlags(const KernelFlags&) = delete;
    KernelFlags& operator=(const KernelFlags&) = delete;

    // Вспомогательные методы
    void initializeFlags();
    void detectCPUFeatures();
    void detectMemoryFeatures();
    void detectNetworkFeatures();
    void detectSecurityFeatures();
    void applyOptimizations();
    void validateConfiguration();

    // Текущая конфигурация
    KernelConfiguration config_;
    
    // Системные возможности
    struct SystemCapabilities {
        bool has_sse;
        bool has_avx;
        bool has_neon;
        bool has_gpu;
        bool has_tpu;
        bool has_encryption;
        bool has_sandbox;
        size_t max_memory;
        size_t max_threads;
        std::vector<std::string> supported_features;
    } capabilities_;

    static std::unique_ptr<KernelFlags> instance_;
    static std::mutex instance_mutex_;
};

} // namespace cloud 