#pragma once

#include "core/kernel_base.hpp"
#include "core/task_manager.hpp"
#include "core/resource_manager.hpp"
#include "core/thread_pool.hpp"
#include "core/error.hpp"
#include "core/system_info.hpp"
#include "core/kernel_flags.hpp"
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <functional>
#include <chrono>
#include <boost/asio.hpp>

namespace cloud {

class CloudIntegration {
public:
    struct CloudConfig {
        std::string provider;
        std::string region;
        std::string api_key;
        std::string secret_key;
        bool auto_scaling;
        size_t min_instances;
        size_t max_instances;
        double scaling_threshold;
        std::chrono::seconds health_check_interval;
    };

    struct InstanceMetrics {
        double cpu_usage;
        size_t memory_usage;
        size_t network_bandwidth;
        size_t disk_io;
        std::chrono::system_clock::time_point last_update;
    };

    CloudIntegration(const CloudConfig& config);
    ~CloudIntegration();

    // Cloud Provider Management
    void initializeProvider();
    void configureAutoScaling(bool enable);
    void setScalingThreshold(double threshold);
    
    // Instance Management
    void startInstance(const std::string& instance_type);
    void stopInstance(const std::string& instance_id);
    void terminateInstance(const std::string& instance_id);
    std::vector<std::string> listInstances() const;
    
    // Resource Management
    void allocateResources(const std::string& instance_id, const ResourceQuota& quota);
    void releaseResources(const std::string& instance_id);
    InstanceMetrics getInstanceMetrics(const std::string& instance_id) const;
    
    // Load Balancing
    void configureLoadBalancer(const std::string& lb_name, const std::vector<std::string>& instances);
    void updateLoadBalancer(const std::string& lb_name, const std::vector<std::string>& instances);
    
    // Monitoring
    void startMonitoring();
    void stopMonitoring();
    void setMetricsCallback(std::function<void(const std::string&, const InstanceMetrics&)> callback);
    
    // Security
    void configureSecurityGroup(const std::string& group_id, const std::vector<std::string>& rules);
    void updateSecurityGroup(const std::string& group_id, const std::vector<std::string>& rules);
    
    // Backup and Recovery
    void createSnapshot(const std::string& instance_id);
    void restoreFromSnapshot(const std::string& snapshot_id, const std::string& instance_id);
    
    // Cost Management
    double getCurrentCost() const;
    void setBudgetLimit(double limit);
    void configureCostOptimization(bool enable);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    
    // Internal methods
    void monitorInstances();
    void evaluateScaling();
    void handleInstanceFailure(const std::string& instance_id);
    void updateInstanceMetrics();
    void cleanupResources();
};

} // namespace cloud 