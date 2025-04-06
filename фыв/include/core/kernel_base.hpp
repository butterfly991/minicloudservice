#pragma once

#include "core/error.hpp"
#include "core/system_info.hpp"
#include "core/kernel_flags.hpp"
#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <atomic>
#include <chrono>

namespace cloud {

class KernelBase {
public:
    struct KernelMetrics {
        uint64_t messages_processed;
        uint64_t messages_failed;
        uint64_t active_tasks;
        double cpu_usage;
        double memory_usage;
        std::chrono::system_clock::time_point last_update;
    };

    KernelBase();
    virtual ~KernelBase();

    // Управление состоянием
    virtual void start();
    virtual void stop();
    virtual void pause();
    virtual void resume();
    bool isRunning() const;
    bool isPaused() const;

    // Обработка сообщений
    virtual void sendMessage(const std::string& message);
    virtual void broadcastMessage(const std::string& message);
    void registerMessageHandler(const std::string& type, 
                              std::function<void(const std::string&)> handler);

    // Конфигурация
    void setMaxThreads(size_t count);
    void setMaxQueueSize(size_t size);
    void setTaskTimeout(std::chrono::milliseconds timeout);
    size_t getMaxThreads() const;
    size_t getMaxQueueSize() const;
    std::chrono::milliseconds getTaskTimeout() const;

    // Метрики
    KernelMetrics getMetrics() const;
    double getLoad() const;

    // Адаптивная настройка
    void enableAdaptiveScaling();
    void disableAdaptiveScaling();
    bool isAdaptiveScalingEnabled() const;

    // Оптимизация
    void setOptimizationFlag(KernelFlags::OptimizationFlag flag);
    void clearOptimizationFlag(KernelFlags::OptimizationFlag flag);
    bool hasOptimizationFlag(KernelFlags::OptimizationFlag flag) const;

    void setCompilationDirective(KernelFlags::CompilationDirective directive);
    void clearCompilationDirective(KernelFlags::CompilationDirective directive);
    bool hasCompilationDirective(KernelFlags::CompilationDirective directive) const;

    // Безопасность
    void setSecurityFlag(KernelFlags::SecurityFlag flag);
    void clearSecurityFlag(KernelFlags::SecurityFlag flag);
    bool hasSecurityFlag(KernelFlags::SecurityFlag flag) const;

    // Отладка
    void setDebugFlag(KernelFlags::DebugFlag flag);
    void clearDebugFlag(KernelFlags::DebugFlag flag);
    bool hasDebugFlag(KernelFlags::DebugFlag flag) const;

protected:
    // Вспомогательные методы
    void updateMetrics();
    void handleError(const Error& error);
    void processMessage(const std::string& message);
    void updateResourceUsage();

private:
    // Состояние ядра
    std::atomic<bool> running_;
    std::atomic<bool> paused_;
    std::atomic<size_t> max_threads_;
    std::atomic<size_t> max_queue_size_;
    std::atomic<std::chrono::milliseconds> task_timeout_;
    std::atomic<bool> adaptive_scaling_;

    // Метрики
    KernelMetrics metrics_;
    std::atomic<double> current_load_;

    // Обработчики сообщений
    std::unordered_map<std::string, std::function<void(const std::string&)>> message_handlers_;

    // Системная информация и флаги
    SystemInfo& system_info_;
    KernelFlags& kernel_flags_;

    // Вспомогательные методы
    void initializeFromSystemInfo();
    void updateScalingParameters();
    void cleanupResources();
    void applyOptimizations();
    void validateConfiguration();
};

} // namespace cloud 