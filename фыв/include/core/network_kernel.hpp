#pragma once

#include "core/kernel_base.hpp"
#include <boost/asio.hpp>
#include <unordered_map>
#include <queue>
#include <functional>

namespace cloud {

class NetworkKernel : public KernelBase {
public:
    NetworkKernel();
    ~NetworkKernel() override;

    // Сетевые операции
    bool startServer(const std::string& address, uint16_t port);
    bool connectToServer(const std::string& address, uint16_t port);
    bool sendData(const std::string& target_id, const std::vector<uint8_t>& data);
    
    // Управление соединениями
    bool isConnected(const std::string& target_id) const;
    void disconnect(const std::string& target_id);
    
    // Переопределение методов базового класса
    void sendMessage(const std::string& target_kernel, const std::string& message) override;
    double getLoad() const override;
    
    // Обработчики событий
    using ConnectionHandler = std::function<void(const std::string&, bool)>;
    using DataHandler = std::function<void(const std::string&, const std::vector<uint8_t>&)>;
    
    void setConnectionHandler(ConnectionHandler handler);
    void setDataHandler(DataHandler handler);

private:
    // Сетевые компоненты
    boost::asio::io_context io_context_;
    std::unique_ptr<boost::asio::ip::tcp::acceptor> acceptor_;
    std::unordered_map<std::string, std::shared_ptr<boost::asio::ip::tcp::socket>> connections_;
    
    // Обработчики событий
    ConnectionHandler connection_handler_;
    DataHandler data_handler_;
    
    // Метрики
    struct Metrics {
        size_t bytes_sent = 0;
        size_t bytes_received = 0;
        size_t active_connections = 0;
        std::chrono::system_clock::time_point last_update;
    } metrics_;
    
    // Очередь задач
    struct Task {
        std::string target_id;
        std::vector<uint8_t> data;
        std::chrono::system_clock::time_point timestamp;
    };
    std::queue<Task> task_queue_;
    
    // Методы для работы с ресурсами
    void initializeResources() override;
    void cleanupResources() override;
    
    // Вспомогательные методы
    void acceptConnection();
    void handleConnection(const std::string& connection_id, 
                        std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void processTasks();
    void updateMetrics();
};

} // namespace cloud 