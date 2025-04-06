#ifndef CORE_HIERARCHY_HPP
#define CORE_HIERARCHY_HPP

#include <string>
#include <vector>
#include <iostream>

// Базовый класс для всех компонентов ядра
class CoreComponent {
public:
    virtual ~CoreComponent() = default;
    virtual void initialize() = 0; // Метод инициализации
    virtual void shutdown() = 0;    // Метод завершения работы
};

// Интерфейс для логирования
class ILogger {
public:
    virtual ~ILogger() = default;
    virtual void log_info(const std::string& message) = 0;
    virtual void log_error(const std::string& message) = 0;
    virtual void log_warning(const std::string& message) = 0;
};

// Интерфейс для управления задачами
class ITaskManager {
public:
    virtual ~ITaskManager() = default;
    virtual void addTask(const std::string& task) = 0;
    virtual void executeTasks() = 0;
};

// Базовый класс для ядер
class KernelBase : public CoreComponent {
public:
    virtual void start() = 0; // Метод для запуска ядра
    virtual void stop() = 0;  // Метод для остановки ядра
};

// Класс для высокоприоритетного ядра
class HighPriorityKernel : public KernelBase {
public:
    void initialize() override {
        std::cout << "HighPriorityKernel initialized." << std::endl;
    }

    void shutdown() override {
        std::cout << "HighPriorityKernel shutting down." << std::endl;
    }

    void start() override {
        std::cout << "HighPriorityKernel started." << std::endl;
    }

    void stop() override {
        std::cout << "HighPriorityKernel stopped." << std::endl;
    }
};

// Класс для среднеприоритетного ядра
class MediumPriorityKernel : public KernelBase {
public:
    void initialize() override {
        std::cout << "MediumPriorityKernel initialized." << std::endl;
    }

    void shutdown() override {
        std::cout << "MediumPriorityKernel shutting down." << std::endl;
    }

    void start() override {
        std::cout << "MediumPriorityKernel started." << std::endl;
    }

    void stop() override {
        std::cout << "MediumPriorityKernel stopped." << std::endl;
    }
};

// Класс для низкоприоритетного ядра
class LowPriorityKernel : public KernelBase {
public:
    void initialize() override {
        std::cout << "LowPriorityKernel initialized." << std::endl;
    }

    void shutdown() override {
        std::cout << "LowPriorityKernel shutting down." << std::endl;
    }

    void start() override {
        std::cout << "LowPriorityKernel started." << std::endl;
    }

    void stop() override {
        std::cout << "LowPriorityKernel stopped." << std::endl;
    }
};

#endif // CORE_HIERARCHY_HPP
