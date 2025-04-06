#include "task_manager.hpp"

namespace cloud {
namespace core {
namespace kernels {
namespace low {

TaskManager::TaskManager(size_t max_tasks) : max_tasks_(max_tasks) {}

void TaskManager::submit_task(Task task) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (tasks_.size() >= max_tasks_) {
        throw std::runtime_error("Task queue is full");
    }
    tasks_.push_back(task);
    condition_.notify_one();
}

TaskManager::Task TaskManager::get_next_task() {
    std::unique_lock<std::mutex> lock(mutex_);
    condition_.wait(lock, [this] { return !tasks_.empty(); });
    Task task = tasks_.front();
    tasks_.erase(tasks_.begin());
    return task;
}

void TaskManager::remove_task(const std::string& task_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = std::remove_if(tasks_.begin(), tasks_.end(),
                             [&](const Task& t) { return t.get_id() == task_id; });

    if (it != tasks_.end()) {
        tasks_.erase(it, tasks_.end());
        std::cout << "Task with ID " << task_id << " has been removed." << std::endl;
    } else {
        throw std::runtime_error("Task not found: " + task_id);
    }
}

bool TaskManager::is_full() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.size() >= max_tasks_;
}

size_t TaskManager::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.size();
}

std::vector<TaskManager::Task> TaskManager::get_all_tasks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_; // Возвращаем копию всех задач
}

} // namespace low
} // namespace kernels
} // namespace core
} // namespace cloud
