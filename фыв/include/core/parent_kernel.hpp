#pragma once

#include "core/kernel_base.hpp"
#include "core/task_manager.hpp"
#include "core/resource_manager.hpp"
#include "core/thread_pool.hpp"
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>
#include <future>
#include <functional>
#include <chrono>

namespace cloud {

class ParentKernel : public KernelBase {
public:
    struct ChildKernelInfo {
        std::shared_ptr<KernelBase> kernel;
        std::string type;
        bool is_active;
        double current_load;
        size_t task_count;
        std::chrono::system_clock::time_point last_heartbeat;
        std::vector<std::string> capabilities;
    };

    struct TaskDistribution {
        std::string kernel_name;
        double load_factor;
        size_t priority;
        std::vector<std::string> required_capabilities;
    };

    struct ResourceQuota {
        size_t max_threads;
        size_t max_memory;
        size_t max_network_bandwidth;
        size_t max_disk_io;
        std::chrono::milliseconds max_cpu_time;
    };

    ParentKernel();
    ~ParentKernel() override;

    // Управление дочерними ядрами
    void registerChildKernel(const std::string& name, std::shared_ptr<KernelBase> kernel);
    void unregisterChildKernel(const std::string& name);
    std::shared_ptr<KernelBase> getChildKernel(const std::string& name) const;
    std::vector<std::string> getChildKernels() const;
    bool isChildKernelActive(const std::string& name) const;

    // Распределение задач
    void setTaskDistribution(const std::string& kernel_name, const TaskDistribution& distribution);
    TaskDistribution getTaskDistribution(const std::string& kernel_name) const;
    void updateTaskDistribution(const std::string& kernel_name, double new_load_factor);
    void balanceTasks();

    // Управление ресурсами
    void setResourceQuota(const std::string& kernel_name, const ResourceQuota& quota);
    ResourceQuota getResourceQuota(const std::string& kernel_name) const;
    void updateResourceQuota(const std::string& kernel_name, const ResourceQuota& new_quota);
    void monitorResourceUsage();

    // Мониторинг и метрики
    struct SystemMetrics {
        double total_cpu_usage;
        size_t total_memory_usage;
        size_t total_network_bandwidth;
        size_t total_disk_io;
        std::unordered_map<std::string, double> kernel_loads;
        std::unordered_map<std::string, size_t> kernel_task_counts;
    };
    SystemMetrics getSystemMetrics() const;
    void updateMetrics();

    // Оптимизация
    void optimizeChildKernel(const std::string& name);
    void optimizeAllChildKernels();
    void setChildKernelOptimizationFlags(const std::string& name,
                                       const std::vector<KernelFlags::OptimizationFlag>& flags);

    // Обработка сообщений
    void broadcastMessage(const std::string& message) override;
    void sendMessage(const std::string& target, const std::string& message) override;
    void registerMessageHandler(const std::string& type,
                              std::function<void(const std::string&)> handler) override;

    // Управление жизненным циклом
    void start() override;
    void stop() override;
    void pause() override;
    void resume() override;

private:
    // Вспомогательные методы
    void initializeChildKernel(const std::string& name);
    void cleanupChildKernel(const std::string& name);
    void monitorChildKernels();
    void checkChildKernelHealth(const std::string& name);
    void recoverChildKernel(const std::string& name);
    void processTaskQueue();
    void distributeTask(const Task& task);
    void handleChildKernelError(const std::string& name, const Error& error);
    void updateChildKernelStatus(const std::string& name, bool is_active);
    void collectChildKernelMetrics();

    // Члены класса
    std::unordered_map<std::string, ChildKernelInfo> child_kernels_;
    std::unordered_map<std::string, TaskDistribution> task_distributions_;
    std::unordered_map<std::string, ResourceQuota> resource_quotas_;
    std::unique_ptr<TaskManager> task_manager_;
    std::unique_ptr<ResourceManager> resource_manager_;
    std::unique_ptr<ThreadPool> thread_pool_;

    std::queue<Task> task_queue_;
    std::mutex task_queue_mutex_;
    std::condition_variable task_queue_cv_;
    std::atomic<bool> task_processing_active_;

    std::thread monitoring_thread_;
    std::thread task_processing_thread_;
    std::chrono::milliseconds monitoring_interval_;
    std::chrono::milliseconds health_check_interval_;

    mutable std::mutex child_kernels_mutex_;
    mutable std::mutex distributions_mutex_;
    mutable std::mutex quotas_mutex_;
    mutable std::mutex metrics_mutex_;

    SystemMetrics current_metrics_;
    std::atomic<bool> metrics_update_needed_;
};

} // namespace cloud 