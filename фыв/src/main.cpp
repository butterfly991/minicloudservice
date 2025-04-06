#include <iostream>
#include <memory>
#include <thread>
#include <csignal>

// Forward declarations
class CoreService;
class NetworkManager;
class StorageManager;
class SecurityManager;
class BlockchainManager;
class APIManager;
class MonitoringManager;

// Глобальные указатели для доступа к основным компонентам
std::unique_ptr<CoreService> g_core;
std::unique_ptr<NetworkManager> g_network;
std::unique_ptr<StorageManager> g_storage;
std::unique_ptr<SecurityManager> g_security;
std::unique_ptr<BlockchainManager> g_blockchain;
std::unique_ptr<APIManager> g_api;
std::unique_ptr<MonitoringManager> g_monitoring;

// Функция для корректного завершения работы
void shutdown_handler(int signal) {
    std::cout << "Получен сигнал завершения работы. Начинаем корректное завершение..." << std::endl;
    
    // Остановка всех компонентов в правильном порядке
    if (g_api) g_api->shutdown();
    if (g_network) g_network->shutdown();
    if (g_storage) g_storage->shutdown();
    if (g_blockchain) g_blockchain->shutdown();
    if (g_monitoring) g_monitoring->shutdown();
    if (g_security) g_security->shutdown();
    if (g_core) g_core->shutdown();
    
    std::cout << "Все компоненты остановлены. Завершение работы." << std::endl;
    exit(0);
}

int main(int argc, char* argv[]) {
    try {
        // Установка обработчиков сигналов
        signal(SIGINT, shutdown_handler);
        signal(SIGTERM, shutdown_handler);
        
        std::cout << "Запуск облачного сервиса..." << std::endl;
        
        // Инициализация компонентов в правильном порядке
        g_core = std::make_unique<CoreService>();
        g_security = std::make_unique<SecurityManager>();
        g_storage = std::make_unique<StorageManager>();
        g_blockchain = std::make_unique<BlockchainManager>();
        g_network = std::make_unique<NetworkManager>();
        g_api = std::make_unique<APIManager>();
        g_monitoring = std::make_unique<MonitoringManager>();
        
        // Запуск всех компонентов
        g_core->start();
        g_security->start();
        g_storage->start();
        g_blockchain->start();
        g_network->start();
        g_api->start();
        g_monitoring->start();
        
        std::cout << "Сервис успешно запущен и готов к работе." << std::endl;
        
        // Основной цикл приложения
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Критическая ошибка: " << e.what() << std::endl;
        shutdown_handler(SIGTERM);
        return 1;
    }
    
    return 0;
} 