#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <chrono>

namespace cloud {

class SystemInfo {
public:
    struct CPUInfo {
        std::string model_name;
        std::string vendor_id;
        uint32_t physical_cores;
        uint32_t logical_cores;
        uint32_t cache_size;
        double base_frequency;
        std::vector<double> core_frequencies;
        std::vector<double> core_temperatures;
        std::vector<double> core_loads;
    };

    struct MemoryInfo {
        uint64_t total_memory;
        uint64_t available_memory;
        uint64_t used_memory;
        uint64_t free_memory;
        uint64_t swap_total;
        uint64_t swap_used;
        uint64_t swap_free;
        double memory_usage_percent;
    };

    struct DiskInfo {
        std::string device_name;
        std::string mount_point;
        uint64_t total_space;
        uint64_t used_space;
        uint64_t free_space;
        double usage_percent;
        std::string filesystem_type;
    };

    struct NetworkInfo {
        std::string interface_name;
        uint64_t bytes_received;
        uint64_t bytes_sent;
        uint64_t packets_received;
        uint64_t packets_sent;
        double bandwidth_usage;
    };

    static SystemInfo& getInstance();

    // Получение информации о системе
    CPUInfo getCPUInfo() const;
    MemoryInfo getMemoryInfo() const;
    std::vector<DiskInfo> getDiskInfo() const;
    std::vector<NetworkInfo> getNetworkInfo() const;

    // Анализ производительности
    double getSystemLoad() const;
    double getCPUUsage() const;
    double getMemoryUsage() const;
    double getDiskUsage() const;
    double getNetworkUsage() const;

    // Рекомендации по настройке
    size_t getOptimalThreadCount() const;
    size_t getOptimalQueueSize() const;
    std::chrono::milliseconds getOptimalTaskTimeout() const;
    size_t getOptimalBufferSize() const;

    // Мониторинг
    void updateMetrics();
    bool isSystemOverloaded() const;
    bool needsScaling() const;

private:
    SystemInfo();
    ~SystemInfo();

    // Запрет копирования
    SystemInfo(const SystemInfo&) = delete;
    SystemInfo& operator=(const SystemInfo&) = delete;

    // Вспомогательные методы
    void initializeSystemInfo();
    void readCPUInfo();
    void readMemoryInfo();
    void readDiskInfo();
    void readNetworkInfo();
    void calculateOptimalSettings();
    void updatePerformanceMetrics();

    // Кэшированные данные
    CPUInfo cpu_info_;
    MemoryInfo memory_info_;
    std::vector<DiskInfo> disk_info_;
    std::vector<NetworkInfo> network_info_;
    
    // Метрики производительности
    double system_load_;
    double cpu_usage_;
    double memory_usage_;
    double disk_usage_;
    double network_usage_;
    
    // Оптимальные настройки
    size_t optimal_thread_count_;
    size_t optimal_queue_size_;
    std::chrono::milliseconds optimal_task_timeout_;
    size_t optimal_buffer_size_;
    
    // Время последнего обновления
    std::chrono::system_clock::time_point last_update_;
    
    static std::unique_ptr<SystemInfo> instance_;
    static std::mutex instance_mutex_;
};

} // namespace cloud 