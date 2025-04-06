#include "core/task_manager.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <random>
#include <chrono>
#include <functional>
#include <string>

namespace cloud {

struct Task {
    std::string id; // Уникальный идентификатор задачи
    std::string type; // Тип задачи
    std::string data; // Данные задачи
    std::string status; // Статус задачи (pending, running, completed, failed)
    std::chrono::system_clock::time_point created_at; // Время создания задачи
    std::chrono::system_clock::time_point completed_at; // Время завершения задачи
    std::chrono::system_clock::time_point started_at; // Время начала выполнения задачи
    std::string assigned_kernel; // Название ядра, которому назначена задача
    std::function<void(const std::string&)> callback; // Функция обратного вызова

    // Дополнительные методы могут быть добавлены здесь
};

TaskManager::TaskManager() {
    metrics_.last_update = std::chrono::system_clock::now();
}

TaskManager::~TaskManager() {
    cleanupOldTasks();
}

std::string TaskManager::submitTask(const std::string& type, const std::string& data,
                                  std::function<void(const std::string&)> callback) {
    Task task;
    task.id = generateTaskId();
    task.type = type;
    task.data = data;
    task.status = "pending";
    task.created_at = std::chrono::system_clock::now();
    task.callback = std::move(callback);
    
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        pending_tasks_.push(task);
    }
    
    metrics_.total_tasks++;
    queue_cv_.notify_one();
    
    return task.id;
}

bool TaskManager::cancelTask(const std::string& task_id) {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    
    // Проверяем, не выполняется ли задача
    auto running_it = running_tasks_.find(task_id);
    if (running_it != running_tasks_.end()) {
        running_it->second.status = "cancelled";
        running_it->second.completed_at = std::chrono::system_clock::now();
        completed_tasks_[task_id] = running_it->second;
        running_tasks_.erase(running_it);
        metrics_.failed_tasks++;
        return true;
    }
    
    // Проверяем очередь ожидающих задач
    std::queue<Task> temp_queue;
    bool found = false;
    
    {
        std::lock_guard<std::mutex> queue_lock(queue_mutex_);
        while (!pending_tasks_.empty()) {
            Task task = pending_tasks_.front();
            pending_tasks_.pop();
            
            if (task.id == task_id) {
                task.status = "cancelled";
                task.completed_at = std::chrono::system_clock::now();
                completed_tasks_[task_id] = task;
                metrics_.failed_tasks++;
                found = true;
            } else {
                temp_queue.push(task);
            }
        }
        
        pending_tasks_ = std::move(temp_queue);
    }
    
    return found;
}

bool TaskManager::isTaskRunning(const std::string& task_id) const {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    return running_tasks_.find(task_id) != running_tasks_.end();
}

Task TaskManager::getTaskStatus(const std::string& task_id) const {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    
    auto running_it = running_tasks_.find(task_id);
    if (running_it != running_tasks_.end()) {
        return running_it->second;
    }
    
    auto completed_it = completed_tasks_.find(task_id);
    if (completed_it != completed_tasks_.end()) {
        return completed_it->second;
    }
    
    // Проверяем очередь ожидающих задач
    std::lock_guard<std::mutex> queue_lock(queue_mutex_);
    std::queue<Task> temp_queue = pending_tasks_;
    while (!temp_queue.empty()) {
        Task task = temp_queue.front();
        temp_queue.pop();
        if (task.id == task_id) {
            return task;
        }
    }
    
    // Если задача не найдена, возвращаем пустую задачу
    return Task{};
}

void TaskManager::processTaskQueue() {
    while (true) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] { return !pending_tasks_.empty(); });
            
            task = pending_tasks_.front();
            pending_tasks_.pop();
        }
        
        if (!task.assigned_kernel.empty()) {
            std::lock_guard<std::mutex> lock(tasks_mutex_);
            task.status = "running";
            task.started_at = std::chrono::system_clock::now();
            running_tasks_[task.id] = task;
        }
    }
}

void TaskManager::assignTask(const std::string& task_id, const std::string& kernel_name) {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    
    auto running_it = running_tasks_.find(task_id);
    if (running_it != running_tasks_.end()) {
        running_it->second.assigned_kernel = kernel_name;
        return;
    }
    
    // Проверяем очередь ожидающих задач
    std::lock_guard<std::mutex> queue_lock(queue_mutex_);
    std::queue<Task> temp_queue;
    bool found = false;
    
    while (!pending_tasks_.empty()) {
        Task task = pending_tasks_.front();
        pending_tasks_.pop();
        
        if (task.id == task_id) {
            task.assigned_kernel = kernel_name;
            found = true;
        }
        
        temp_queue.push(task);
    }
    
    pending_tasks_ = std::move(temp_queue);
}

std::vector<Task> TaskManager::getPendingTasks() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    std::vector<Task> tasks;
    std::queue<Task> temp_queue = pending_tasks_;
    
    while (!temp_queue.empty()) {
        tasks.push_back(temp_queue.front());
        temp_queue.pop();
    }
    
    return tasks;
}

std::vector<Task> TaskManager::getRunningTasks() const {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    std::vector<Task> tasks;
    for (const auto& pair : running_tasks_) {
        tasks.push_back(pair.second);
    }
    return tasks;
}

size_t TaskManager::getQueueSize() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return pending_tasks_.size();
}

size_t TaskManager::getActiveTasks() const {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    return running_tasks_.size();
}

double TaskManager::getAverageTaskTime() const {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    if (metrics_.completed_tasks == 0) {
        return 0.0;
    }
    return metrics_.total_processing_time / metrics_.completed_tasks;
}

std::string TaskManager::generateTaskId() const {
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

void TaskManager::updateTaskStatus(const std::string& task_id, const std::string& status) {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    
    auto it = running_tasks_.find(task_id);
    if (it != running_tasks_.end()) {
        it->second.status = status;
        if (status == "completed" || status == "failed") {
            it->second.completed_at = std::chrono::system_clock::now();
            completed_tasks_[task_id] = it->second;
            running_tasks_.erase(it);
            
            if (status == "completed") {
                metrics_.completed_tasks++;
            } else {
                metrics_.failed_tasks++;
            }
            
            if (it->second.callback) {
                it->second.callback(status);
            }
        }
    }
}

void TaskManager::updateMetrics(const Task& task) {
    if (task.completed_at != std::chrono::system_clock::time_point()) {
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            task.completed_at - task.started_at).count();
        metrics_.total_processing_time += duration;
    }
}

void TaskManager::cleanupOldTasks() {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    
    auto now = std::chrono::system_clock::now();
    auto max_age = std::chrono::hours(24); // Храним задачи 24 часа
    
    for (auto it = completed_tasks_.begin(); it != completed_tasks_.end();) {
        if (now - it->second.completed_at > max_age) {
            it = completed_tasks_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace cloud 