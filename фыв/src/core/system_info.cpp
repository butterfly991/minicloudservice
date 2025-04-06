#include "core/system_info.hpp"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <thread>
#include <chrono>
#include <algorithm>
#include <numeric>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <dirent.h>

namespace cloud {

std::unique_ptr<SystemInfo> SystemInfo::instance_;
std::mutex SystemInfo::instance_mutex_;

SystemInfo& SystemInfo::getInstance() {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    if (!instance_) {
        instance_ = std::make_unique<SystemInfo>();
    }
    return *instance_;
}

SystemInfo::SystemInfo() {
    initializeSystemInfo();
}

SystemInfo::~SystemInfo() = default;

void SystemInfo::initializeSystemInfo() {
    readCPUInfo();
    readMemoryInfo();
    readDiskInfo();
    readNetworkInfo();
    calculateOptimalSettings();
    updatePerformanceMetrics();
    last_update_ = std::chrono::system_clock::now();
}

void SystemInfo::readCPUInfo() {
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line;
    std::stringstream ss;
    
    while (std::getline(cpuinfo, line)) {
        if (line.find("model name") != std::string::npos) {
            size_t pos = line.find(":");
            if (pos != std::string::npos) {
                cpu_info_.model_name = line.substr(pos + 1);
            }
        }
        else if (line.find("vendor_id") != std::string::npos) {
            size_t pos = line.find(":");
            if (pos != std::string::npos) {
                cpu_info_.vendor_id = line.substr(pos + 1);
            }
        }
        else if (line.find("cache size") != std::string::npos) {
            size_t pos = line.find(":");
            if (pos != std::string::npos) {
                std::string cache = line.substr(pos + 1);
                size_t size_pos = cache.find("KB");
                if (size_pos != std::string::npos) {
                    cpu_info_.cache_size = std::stoul(cache.substr(0, size_pos));
                }
            }
        }
    }
    
    cpu_info_.physical_cores = std::thread::hardware_concurrency() / 2;
    cpu_info_.logical_cores = std::thread::hardware_concurrency();
    
    // Чтение частот процессора
    for (uint32_t i = 0; i < cpu_info_.logical_cores; ++i) {
        std::string freq_path = "/sys/devices/system/cpu/cpu" + std::to_string(i) + "/cpufreq/scaling_cur_freq";
        std::ifstream freq_file(freq_path);
        if (freq_file.is_open()) {
            std::string freq;
            std::getline(freq_file, freq);
            cpu_info_.core_frequencies.push_back(std::stod(freq) / 1000.0);
        }
    }
    
    // Чтение температур ядер
    DIR* dir = opendir("/sys/class/thermal");
    if (dir != nullptr) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (std::string(entry->d_name).find("thermal_zone") != std::string::npos) {
                std::string temp_path = "/sys/class/thermal/" + std::string(entry->d_name) + "/temp";
                std::ifstream temp_file(temp_path);
                if (temp_file.is_open()) {
                    std::string temp;
                    std::getline(temp_file, temp);
                    cpu_info_.core_temperatures.push_back(std::stod(temp) / 1000.0);
                }
            }
        }
        closedir(dir);
    }
}

void SystemInfo::readMemoryInfo() {
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        memory_info_.total_memory = si.totalram;
        memory_info_.free_memory = si.freeram;
        memory_info_.used_memory = memory_info_.total_memory - memory_info_.free_memory;
        memory_info_.available_memory = si.bufferram;
        
        memory_info_.swap_total = si.totalswap;
        memory_info_.swap_free = si.freeswap;
        memory_info_.swap_used = memory_info_.swap_total - memory_info_.swap_free;
        
        memory_info_.memory_usage_percent = (static_cast<double>(memory_info_.used_memory) / 
                                           static_cast<double>(memory_info_.total_memory)) * 100.0;
    }
}

void SystemInfo::readDiskInfo() {
    struct statvfs stat;
    if (statvfs("/", &stat) == 0) {
        DiskInfo disk;
        disk.device_name = stat.f_mntfromname;
        disk.mount_point = stat.f_mntonname;
        disk.total_space = stat.f_blocks * stat.f_frsize;
        disk.free_space = stat.f_bfree * stat.f_frsize;
        disk.used_space = disk.total_space - disk.free_space;
        disk.usage_percent = (static_cast<double>(disk.used_space) / 
                            static_cast<double>(disk.total_space)) * 100.0;
        disk.filesystem_type = stat.f_fstype;
        
        disk_info_.push_back(disk);
    }
}

void SystemInfo::readNetworkInfo() {
    std::ifstream netdev("/proc/net/dev");
    std::string line;
    std::getline(netdev, line); // Пропускаем заголовки
    std::getline(netdev, line);
    
    while (std::getline(netdev, line)) {
        std::stringstream ss(line);
        std::string interface;
        uint64_t bytes_received, packets_received, bytes_sent, packets_sent;
        
        ss >> interface;
        if (interface.find(":") != std::string::npos) {
            interface = interface.substr(0, interface.length() - 1);
            if (interface != "lo") { // Пропускаем локальный интерфейс
                ss >> bytes_received >> packets_received >> bytes_sent >> packets_sent;
                
                NetworkInfo net;
                net.interface_name = interface;
                net.bytes_received = bytes_received;
                net.packets_received = packets_received;
                net.bytes_sent = bytes_sent;
                net.packets_sent = packets_sent;
                
                network_info_.push_back(net);
            }
        }
    }
}

void SystemInfo::calculateOptimalSettings() {
    // Оптимальное количество потоков
    optimal_thread_count_ = cpu_info_.logical_cores;
    
    // Оптимальный размер очереди
    optimal_queue_size_ = optimal_thread_count_ * 100;
    
    // Оптимальный таймаут задачи
    optimal_task_timeout_ = std::chrono::milliseconds(1000);
    
    // Оптимальный размер буфера
    optimal_buffer_size_ = std::min(memory_info_.available_memory / 1000, 
                                  static_cast<uint64_t>(1024 * 1024)); // 1MB или меньше
}

void SystemInfo::updatePerformanceMetrics() {
    // Обновление загрузки CPU
    std::ifstream loadavg("/proc/loadavg");
    std::string load;
    std::getline(loadavg, load);
    system_load_ = std::stod(load);
    
    // Обновление использования CPU
    cpu_usage_ = std::accumulate(cpu_info_.core_loads.begin(), 
                                cpu_info_.core_loads.end(), 0.0) / 
                 cpu_info_.core_loads.size();
    
    // Обновление использования памяти
    memory_usage_ = memory_info_.memory_usage_percent;
    
    // Обновление использования диска
    if (!disk_info_.empty()) {
        disk_usage_ = disk_info_[0].usage_percent;
    }
    
    // Обновление использования сети
    if (!network_info_.empty()) {
        network_usage_ = std::accumulate(network_info_.begin(), 
                                       network_info_.end(), 0.0,
                                       [](double acc, const NetworkInfo& net) {
                                           return acc + net.bandwidth_usage;
                                       }) / network_info_.size();
    }
}

void SystemInfo::updateMetrics() {
    auto now = std::chrono::system_clock::now();
    if (now - last_update_ > std::chrono::seconds(1)) {
        readCPUInfo();
        readMemoryInfo();
        readDiskInfo();
        readNetworkInfo();
        updatePerformanceMetrics();
        last_update_ = now;
    }
}

bool SystemInfo::isSystemOverloaded() const {
    return system_load_ > cpu_info_.logical_cores * 0.8 || // 80% загрузки CPU
           memory_usage_ > 90.0 || // 90% использования памяти
           disk_usage_ > 90.0; // 90% использования диска
}

bool SystemInfo::needsScaling() const {
    return system_load_ > cpu_info_.logical_cores * 0.7 || // 70% загрузки CPU
           memory_usage_ > 80.0 || // 80% использования памяти
           disk_usage_ > 80.0; // 80% использования диска
}

// Геттеры
SystemInfo::CPUInfo SystemInfo::getCPUInfo() const { return cpu_info_; }
SystemInfo::MemoryInfo SystemInfo::getMemoryInfo() const { return memory_info_; }
std::vector<SystemInfo::DiskInfo> SystemInfo::getDiskInfo() const { return disk_info_; }
std::vector<SystemInfo::NetworkInfo> SystemInfo::getNetworkInfo() const { return network_info_; }
double SystemInfo::getSystemLoad() const { return system_load_; }
double SystemInfo::getCPUUsage() const { return cpu_usage_; }
double SystemInfo::getMemoryUsage() const { return memory_usage_; }
double SystemInfo::getDiskUsage() const { return disk_usage_; }
double SystemInfo::getNetworkUsage() const { return network_usage_; }
size_t SystemInfo::getOptimalThreadCount() const { return optimal_thread_count_; }
size_t SystemInfo::getOptimalQueueSize() const { return optimal_queue_size_; }
std::chrono::milliseconds SystemInfo::getOptimalTaskTimeout() const { return optimal_task_timeout_; }
size_t SystemInfo::getOptimalBufferSize() const { return optimal_buffer_size_; }

} // namespace cloud 