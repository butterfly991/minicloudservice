#include "core/resource_manager.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <random>
#include <chrono>

namespace cloud {

ResourceManager::ResourceManager() {
    metrics_.last_update = std::chrono::system_clock::now();
}

ResourceManager::~ResourceManager() {
    cleanupUnusedResources();
}

std::string ResourceManager::allocateResource(const std::string& type, const std::string& owner,
                                            const std::unordered_map<std::string, std::string>& properties) {
    if (!validateResourceType(type)) {
        std::cerr << "ResourceManager: Неизвестный тип ресурса: " << type << std::endl;
        metrics_.failed_allocations++;
        return "";
    }
    
    std::lock_guard<std::mutex> lock(resources_mutex_);
    
    // Проверяем доступность ресурсов в пуле
    auto pool_it = resource_pools_.find(type);
    if (pool_it != resource_pools_.end()) {
        size_t current_usage = type_to_resources_[type].size();
        if (current_usage >= pool_it->second) {
            std::cerr << "ResourceManager: Пул ресурсов типа " << type << " переполнен" << std::endl;
            metrics_.failed_allocations++;
            return "";
        }
    }
    
    Resource resource;
    resource.id = generateResourceId();
    resource.type = type;
    resource.status = "allocated";
    resource.owner = owner;
    resource.allocated_at = std::chrono::system_clock::now();
    resource.last_used = resource.allocated_at;
    resource.properties = properties;
    
    resources_[resource.id] = resource;
    type_to_resources_[type].insert(resource.id);
    
    metrics_.total_allocations++;
    updateMetrics("allocate");
    
    return resource.id;
}

bool ResourceManager::releaseResource(const std::string& resource_id) {
    std::lock_guard<std::mutex> lock(resources_mutex_);
    
    auto it = resources_.find(resource_id);
    if (it == resources_.end()) {
        std::cerr << "ResourceManager: Ресурс не найден: " << resource_id << std::endl;
        return false;
    }
    
    if (it->second.status != "allocated") {
        std::cerr << "ResourceManager: Ресурс не выделен: " << resource_id << std::endl;
        return false;
    }
    
    // Удаляем ресурс из всех индексов
    type_to_resources_[it->second.type].erase(resource_id);
    resources_.erase(it);
    
    metrics_.total_releases++;
    updateMetrics("release");
    
    return true;
}

bool ResourceManager::isResourceAllocated(const std::string& resource_id) const {
    std::lock_guard<std::mutex> lock(resources_mutex_);
    auto it = resources_.find(resource_id);
    return it != resources_.end() && it->second.status == "allocated";
}

Resource ResourceManager::getResourceInfo(const std::string& resource_id) const {
    std::lock_guard<std::mutex> lock(resources_mutex_);
    auto it = resources_.find(resource_id);
    return it != resources_.end() ? it->second : Resource{};
}

void ResourceManager::addResourcePool(const std::string& type, size_t capacity) {
    std::lock_guard<std::mutex> lock(resources_mutex_);
    resource_pools_[type] = capacity;
    type_to_resources_[type] = std::set<std::string>();
}

void ResourceManager::removeResourcePool(const std::string& type) {
    std::lock_guard<std::mutex> lock(resources_mutex_);
    
    // Освобождаем все ресурсы данного типа
    for (const auto& resource_id : type_to_resources_[type]) {
        resources_.erase(resource_id);
    }
    
    resource_pools_.erase(type);
    type_to_resources_.erase(type);
}

size_t ResourceManager::getPoolCapacity(const std::string& type) const {
    std::lock_guard<std::mutex> lock(resources_mutex_);
    auto it = resource_pools_.find(type);
    return it != resource_pools_.end() ? it->second : 0;
}

size_t ResourceManager::getPoolUsage(const std::string& type) const {
    std::lock_guard<std::mutex> lock(resources_mutex_);
    auto it = type_to_resources_.find(type);
    return it != type_to_resources_.end() ? it->second.size() : 0;
}

std::vector<Resource> ResourceManager::getAllocatedResources() const {
    std::lock_guard<std::mutex> lock(resources_mutex_);
    std::vector<Resource> allocated;
    
    for (const auto& pair : resources_) {
        if (pair.second.status == "allocated") {
            allocated.push_back(pair.second);
        }
    }
    
    return allocated;
}

std::vector<Resource> ResourceManager::getAvailableResources() const {
    std::lock_guard<std::mutex> lock(resources_mutex_);
    std::vector<Resource> available;
    
    for (const auto& pair : resources_) {
        if (pair.second.status != "allocated") {
            available.push_back(pair.second);
        }
    }
    
    return available;
}

double ResourceManager::getResourceUtilization(const std::string& type) const {
    std::lock_guard<std::mutex> lock(resources_mutex_);
    
    auto pool_it = resource_pools_.find(type);
    if (pool_it == resource_pools_.end() || pool_it->second == 0) {
        return 0.0;
    }
    
    auto usage_it = type_to_resources_.find(type);
    if (usage_it == type_to_resources_.end()) {
        return 0.0;
    }
    
    return static_cast<double>(usage_it->second.size()) / pool_it->second;
}

void ResourceManager::updateResourceUsage(const std::string& resource_id) {
    std::lock_guard<std::mutex> lock(resources_mutex_);
    
    auto it = resources_.find(resource_id);
    if (it != resources_.end()) {
        it->second.last_used = std::chrono::system_clock::now();
    }
}

std::string ResourceManager::generateResourceId() const {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    
    std::stringstream ss;
    ss << std::hex;
    for (int i = 0; i < 16; i++) {
        ss << dis(gen);
    }
    
    return ss.str();
}

bool ResourceManager::validateResourceType(const std::string& type) const {
    return !type.empty() && type.length() <= 64;
}

void ResourceManager::cleanupUnusedResources() {
    std::lock_guard<std::mutex> lock(resources_mutex_);
    
    auto now = std::chrono::system_clock::now();
    auto max_unused_time = std::chrono::hours(24); // Ресурсы неиспользуемые более 24 часов
    
    for (auto it = resources_.begin(); it != resources_.end();) {
        if (it->second.status != "allocated" && 
            now - it->second.last_used > max_unused_time) {
            type_to_resources_[it->second.type].erase(it->first);
            it = resources_.erase(it);
        } else {
            ++it;
        }
    }
}

void ResourceManager::updateMetrics(const std::string& operation) {
    metrics_.last_update = std::chrono::system_clock::now();
}

} // namespace cloud 