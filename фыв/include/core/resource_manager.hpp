#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <set>
#include <chrono>

namespace cloud {

struct Resource {
    std::string id;
    std::string type;
    std::string status;
    std::string owner;
    std::chrono::system_clock::time_point allocated_at;
    std::chrono::system_clock::time_point last_used;
    std::unordered_map<std::string, std::string> properties;
};

class ResourceManager {
public:
    ResourceManager();
    ~ResourceManager();

    // Управление ресурсами
    std::string allocateResource(const std::string& type, const std::string& owner,
                               const std::unordered_map<std::string, std::string>& properties = {});
    bool releaseResource(const std::string& resource_id);
    bool isResourceAllocated(const std::string& resource_id) const;
    Resource getResourceInfo(const std::string& resource_id) const;
    
    // Управление пулами ресурсов
    void addResourcePool(const std::string& type, size_t capacity);
    void removeResourcePool(const std::string& type);
    size_t getPoolCapacity(const std::string& type) const;
    size_t getPoolUsage(const std::string& type) const;
    
    // Метрики и мониторинг
    std::vector<Resource> getAllocatedResources() const;
    std::vector<Resource> getAvailableResources() const;
    double getResourceUtilization(const std::string& type) const;
    void updateResourceUsage(const std::string& resource_id);
    
private:
    // Реестр ресурсов
    std::unordered_map<std::string, Resource> resources_;
    std::unordered_map<std::string, size_t> resource_pools_;
    std::unordered_map<std::string, std::set<std::string>> type_to_resources_;
    
    // Синхронизация
    mutable std::mutex resources_mutex_;
    
    // Метрики
    struct Metrics {
        size_t total_allocations = 0;
        size_t total_releases = 0;
        size_t failed_allocations = 0;
        std::chrono::system_clock::time_point last_update;
    } metrics_;
    
    // Вспомогательные методы
    std::string generateResourceId() const;
    bool validateResourceType(const std::string& type) const;
    void cleanupUnusedResources();
    void updateMetrics(const std::string& operation);
};

} // namespace cloud 