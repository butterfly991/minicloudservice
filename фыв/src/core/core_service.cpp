#include "core/core_service.hpp"
#include "core/task_manager.hpp"
#include "core/resource_manager.hpp"
#include "core/event_bus.hpp"

#include <iostream>
#include <chrono>

namespace cloud {

CoreService::CoreService() : running_(false) {
    initializeComponents();
}

CoreService::~CoreService() {
    if (running_) {
        shutdown();
    }
    cleanupComponents();
}

void CoreService::start() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (running_) {
        return;
    }

    running_ = true;
    std::cout << "CoreService: Запуск сервиса..." << std::endl;

    // Запуск обработки событий в отдельном потоке
    std::thread event_thread(&CoreService::processEvents, this);
    event_thread.detach();
}

void CoreService::shutdown() {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (!running_) {
            return;
        }
        running_ = false;
    }
    
    state_cv_.notify_all();
    std::cout << "CoreService: Остановка сервиса..." << std::endl;
}

bool CoreService::isRunning() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return running_;
}

bool CoreService::allocateResources(const std::string& service_id, 
                                  const std::vector<std::string>& resources) {
    return resource_manager_->allocate(service_id, resources);
}

bool CoreService::releaseResources(const std::string& service_id) {
    return resource_manager_->release(service_id);
}

bool CoreService::submitTask(const std::string& task_id, 
                           const std::string& task_type, 
                           const std::string& data) {
    return task_manager_->submit(task_id, task_type, data);
}

bool CoreService::cancelTask(const std::string& task_id) {
    return task_manager_->cancel(task_id);
}

std::vector<std::string> CoreService::getActiveServices() const {
    return resource_manager_->getActiveServices();
}

std::vector<std::string> CoreService::getRunningTasks() const {
    return task_manager_->getRunningTasks();
}

double CoreService::getSystemLoad() const {
    return resource_manager_->getSystemLoad();
}

void CoreService::initializeComponents() {
    task_manager_ = std::make_unique<TaskManager>();
    resource_manager_ = std::make_unique<ResourceManager>();
    event_bus_ = std::make_unique<EventBus>();
}

void CoreService::cleanupComponents() {
    task_manager_.reset();
    resource_manager_.reset();
    event_bus_.reset();
}

void CoreService::processEvents() {
    while (true) {
        {
            std::unique_lock<std::mutex> lock(state_mutex_);
            if (!running_) {
                break;
            }
        }

        // Обработка событий из шины событий
        event_bus_->processEvents();

        // Небольшая задержка для предотвращения перегрузки CPU
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

} // namespace cloud 