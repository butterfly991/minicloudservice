#pragma once

#include <queue>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <vector>

namespace cloud {
namespace core {
namespace kernels {
namespace low {

class TaskManager {
public:
    using Task = std::function<void()>;

    TaskManager(size_t max_tasks);
    void submit_task(Task task);
    Task get_next_task();
    void remove_task(const std::string& task_id);
    bool is_full() const;

private:
    std::vector<Task> tasks_;
    size_t max_tasks_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
};

} // namespace low
} // namespace kernels
} // namespace core
} // namespace cloud
