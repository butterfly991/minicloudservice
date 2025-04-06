#pragma once

#include <mutex>
#include <stdexcept>
#include <iostream>

namespace cloud {
namespace core {
namespace kernels {
namespace low {

class ResourceManager {
public:
    ResourceManager(double max_cpu, size_t max_memory);
    void check_resources();
    void apply_limits(double current_cpu, size_t current_memory);
    void set_limits(double max_cpu, size_t max_memory);
    double get_max_cpu() const;
    size_t get_max_memory() const;

private:
    double max_cpu_;
    size_t max_memory_;
    mutable std::mutex mutex_;
};

} // namespace low
} // namespace kernels
} // namespace core
} // namespace cloud
