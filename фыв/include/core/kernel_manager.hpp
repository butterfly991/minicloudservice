#pragma once

#include "core/kernel_base.hpp"
#include "core/kernel_flags.hpp"
#include "core/system_info.hpp"
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#include <functional>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>

namespace cloud {

class KernelManager {
public:
    struct KernelConfig {
        std::string name;
        std::string type;
        size_t max_threads;
        size_t max_queue_size;
        std::chrono::milliseconds task_timeout;
        bool auto_recovery;
        bool load_balancing;
        std::unordered_map<std::string, std::string> custom_config;
    };

    struct KernelStatus {
        std::string name;
        bool is_running;
        bool is_healthy;
        size_t restart_count;
        double current_load;
        std::chrono::system_clock::time_point last_health_check;
        std::vector<std::string> active_processes;
    };

    static KernelManager& getInstance();

    // Управление ядрами
    void registerKernel(const std::string& name, std::shared_ptr<KernelBase> kernel);
    void unregisterKernel(const std::string& name);
    std::shared_ptr<KernelBase> getKernel(const std::string& name) const;
    std::vector<std::string> getRegisteredKernels() const;

    // Конфигурация
    void setKernelConfig(const std::string& name, const KernelConfig& config);
    KernelConfig getKernelConfig(const std::string& name) const;
    void setAutoRecovery(bool enabled);
    void setLoadBalancing(bool enabled);

    // Управление жизненным циклом
    void startAll();
    void stopAll();
    void pauseAll();
    void resumeAll();
    void startKernel(const std::string& name);
    void stopKernel(const std::string& name);
    void pauseKernel(const std::string& name);
    void resumeKernel(const std::string& name);

    // Мониторинг
    KernelStatus getKernelStatus(const std::string& name) const;
    std::vector<KernelStatus> getAllKernelStatuses() const;
    bool isKernelHealthy(const std::string& name) const;
    double getSystemLoad() const;

    // Обработка ошибок
    void setErrorHandler(std::function<void(const Error&)> handler);
    void handleKernelError(const std::string& name, const Error& error);

    // Оптимизация
    void optimizeKernel(const std::string& name);
    void optimizeAllKernels();
    void setKernelOptimizationFlags(const std::string& name, 
                                  const std::vector<KernelFlags::OptimizationFlag>& flags);

private:
    KernelManager();
    ~KernelManager();

    // Запрет копирования
    KernelManager(const KernelManager&) = delete;
    KernelManager& operator=(const KernelManager&) = delete;

    // Вспомогательные методы
    void initializeKernel(const std::string& name);
    void cleanupKernel(const std::string& name);
    void monitorKernels();
    void checkKernelHealth(const std::string& name);
    void recoverKernel(const std::string& name);
    void balanceLoad();
    void updateMetrics();

    // Члены класса
    std::unordered_map<std::string, std::shared_ptr<KernelBase>> kernels_;
    std::unordered_map<std::string, KernelConfig> kernel_configs_;
    std::unordered_map<std::string, KernelStatus> kernel_statuses_;
    std::function<void(const Error&)> error_handler_;
    
    std::atomic<bool> auto_recovery_enabled_;
    std::atomic<bool> load_balancing_enabled_;
    std::atomic<bool> monitoring_active_;
    
    mutable std::mutex kernels_mutex_;
    mutable std::mutex configs_mutex_;
    mutable std::mutex statuses_mutex_;
    
    std::thread monitoring_thread_;
    std::chrono::milliseconds health_check_interval_;
    
    static std::unique_ptr<KernelManager> instance_;
    static std::mutex instance_mutex_;
};

} // namespace cloud 