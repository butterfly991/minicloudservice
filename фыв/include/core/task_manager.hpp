#pragma once

#include <string>
#include <vector>
#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <thread>
#include <unordered_map>

namespace cloud {

struct Task {
    std::string id;
    std::string type;
    std::string data;
    std::string status;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point started_at;
    std::chrono::system_clock::time_point completed_at;
    std::string assigned_kernel;
    std::function<void(const std::string&)> callback;
};

class TaskManager {
public:
    TaskManager();
    ~TaskManager();

    // Управление задачами
    std::string submitTask(const std::string& type, const std::string& data,
                          std::function<void(const std::string&)> callback = nullptr);
    bool cancelTask(const std::string& task_id);
    bool isTaskRunning(const std::string& task_id) const;
    Task getTaskStatus(const std::string& task_id) const;
    
    // Управление очередью задач
    void processTaskQueue();
    void assignTask(const std::string& task_id, const std::string& kernel_name);
    std::vector<Task> getPendingTasks() const;
    std::vector<Task> getRunningTasks() const;
    
    // Метрики и мониторинг
    size_t getQueueSize() const;
    size_t getActiveTasks() const;
    double getAverageTaskTime() const;
    
private:
    // Очереди задач
    std::queue<Task> pending_tasks_;
    std::unordered_map<std::string, Task> running_tasks_;
    std::unordered_map<std::string, Task> completed_tasks_;
    
    // Синхронизация
    mutable std::mutex queue_mutex_;
    mutable std::mutex tasks_mutex_;
    std::condition_variable queue_cv_;
    
    // Метрики
    struct Metrics {
        size_t total_tasks = 0;
        size_t completed_tasks = 0;
        size_t failed_tasks = 0;
        double total_processing_time = 0.0;
        std::chrono::system_clock::time_point last_update;
    } metrics_;
    
    // Вспомогательные методы
    std::string generateTaskId() const;
    void updateTaskStatus(const std::string& task_id, const std::string& status);
    void updateMetrics(const Task& task);
    void cleanupOldTasks();
};

} // namespace cloud 