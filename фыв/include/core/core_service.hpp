#pragma once

#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace cloud {

class TaskManager;
class ResourceManager;
class EventBus;

class CoreService {
public:
    CoreService();
    ~CoreService();

    // Запрет копирования
    CoreService(const CoreService&) = delete;
    CoreService& operator=(const CoreService&) = delete;

    // Основные методы управления
    void start();
    void shutdown();
    bool isRunning() const;

    // Методы управления ресурсами
    bool allocateResources(const std::string& service_id, const std::vector<std::string>& resources);
    bool releaseResources(const std::string& service_id);
    
    // Методы управления задачами
    bool submitTask(const std::string& task_id, const std::string& task_type, const std::string& data);
    bool cancelTask(const std::string& task_id);
    
    // Методы мониторинга
    std::vector<std::string> getActiveServices() const;
    std::vector<std::string> getRunningTasks() const;
    double getSystemLoad() const;

private:
    // Внутренние компоненты
    std::unique_ptr<TaskManager> task_manager_;
    std::unique_ptr<ResourceManager> resource_manager_;
    std::unique_ptr<EventBus> event_bus_;

    // Состояние сервиса
    bool running_;
    std::mutex state_mutex_;
    std::condition_variable state_cv_;
    
    // Вспомогательные методы
    void initializeComponents();
    void cleanupComponents();
    void processEvents();
};

} // namespace cloud 