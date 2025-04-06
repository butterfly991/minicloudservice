#pragma once

#include "core/kernel_base.hpp"
#include "core/resource_manager.hpp"
#include "core/system_info.hpp"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include <chrono>

namespace cloud {

struct ContainerConfig {
    std::string image;
    std::string name;
    std::vector<std::string> environment;
    std::vector<std::string> volumes;
    std::vector<std::string> ports;
    ResourceQuota resources;
    bool privileged;
    bool auto_restart;
    std::chrono::seconds health_check_interval;
};

struct ContainerMetrics {
    double cpu_usage;
    size_t memory_usage;
    size_t network_rx;
    size_t network_tx;
    size_t disk_read;
    size_t disk_write;
    std::chrono::system_clock::time_point last_update;
};

class ContainerManager {
public:
    ContainerManager();
    ~ContainerManager();

    // Container Lifecycle
    std::string createContainer(const ContainerConfig& config);
    void startContainer(const std::string& container_id);
    void stopContainer(const std::string& container_id);
    void removeContainer(const std::string& container_id);
    void restartContainer(const std::string& container_id);

    // Container Management
    void updateContainerResources(const std::string& container_id, const ResourceQuota& quota);
    void attachToContainer(const std::string& container_id);
    void detachFromContainer(const std::string& container_id);
    std::vector<std::string> listContainers() const;

    // Container Monitoring
    ContainerMetrics getContainerMetrics(const std::string& container_id) const;
    void setMetricsCallback(std::function<void(const std::string&, const ContainerMetrics&)> callback);
    void startMonitoring();
    void stopMonitoring();

    // Container Optimization
    void optimizeContainer(const std::string& container_id);
    void setContainerPriority(const std::string& container_id, int priority);
    void enableContainerGPU(const std::string& container_id, bool enable);

    // Container Security
    void setContainerCapabilities(const std::string& container_id, const std::vector<std::string>& capabilities);
    void setContainerSeccompProfile(const std::string& container_id, const std::string& profile);
    void setContainerAppArmorProfile(const std::string& container_id, const std::string& profile);

    // Container Networking
    void connectToNetwork(const std::string& container_id, const std::string& network);
    void disconnectFromNetwork(const std::string& container_id, const std::string& network);
    void setContainerIP(const std::string& container_id, const std::string& ip);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    
    // Internal methods
    void monitorContainers();
    void handleContainerFailure(const std::string& container_id);
    void cleanupContainerResources(const std::string& container_id);
    void updateContainerMetrics();
};

} // namespace cloud 