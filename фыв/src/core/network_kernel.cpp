#include "core/network_kernel.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <boost/endian/conversion.hpp>

namespace cloud {

NetworkKernel::NetworkKernel() : KernelBase("NetworkKernel") {
    metrics_.last_update = std::chrono::system_clock::now();
}

NetworkKernel::~NetworkKernel() {
    cleanupResources();
}

bool NetworkKernel::startServer(const std::string& address, uint16_t port) {
    try {
        boost::asio::ip::tcp::endpoint endpoint(
            boost::asio::ip::make_address(address), port);
        
        acceptor_ = std::make_unique<boost::asio::ip::tcp::acceptor>(io_context_, endpoint);
        acceptor_->listen();
        
        std::cout << "NetworkKernel: Сервер запущен на " << address << ":" << port << std::endl;
        acceptConnection();
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "NetworkKernel: Ошибка запуска сервера: " << e.what() << std::endl;
        return false;
    }
}

bool NetworkKernel::connectToServer(const std::string& address, uint16_t port) {
    try {
        auto socket = std::make_shared<boost::asio::ip::tcp::socket>(io_context_);
        boost::asio::ip::tcp::endpoint endpoint(
            boost::asio::ip::make_address(address), port);
        
        socket->connect(endpoint);
        std::string connection_id = address + ":" + std::to_string(port);
        connections_[connection_id] = socket;
        
        if (connection_handler_) {
            connection_handler_(connection_id, true);
        }
        
        std::cout << "NetworkKernel: Установлено соединение с " << connection_id << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "NetworkKernel: Ошибка подключения: " << e.what() << std::endl;
        return false;
    }
}

bool NetworkKernel::sendData(const std::string& target_id, const std::vector<uint8_t>& data) {
    auto it = connections_.find(target_id);
    if (it == connections_.end()) {
        std::cerr << "NetworkKernel: Соединение с " << target_id << " не найдено" << std::endl;
        return false;
    }

    try {
        // Добавляем размер данных в начало пакета
        uint32_t size = boost::endian::native_to_big(static_cast<uint32_t>(data.size()));
        std::vector<uint8_t> packet(sizeof(size) + data.size());
        std::memcpy(packet.data(), &size, sizeof(size));
        std::memcpy(packet.data() + sizeof(size), data.data(), data.size());

        boost::asio::write(*it->second, boost::asio::buffer(packet));
        metrics_.bytes_sent += packet.size();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "NetworkKernel: Ошибка отправки данных: " << e.what() << std::endl;
        return false;
    }
}

bool NetworkKernel::isConnected(const std::string& target_id) const {
    return connections_.find(target_id) != connections_.end();
}

void NetworkKernel::disconnect(const std::string& target_id) {
    auto it = connections_.find(target_id);
    if (it != connections_.end()) {
        try {
            it->second->close();
            connections_.erase(it);
            if (connection_handler_) {
                connection_handler_(target_id, false);
            }
        } catch (const std::exception& e) {
            std::cerr << "NetworkKernel: Ошибка отключения: " << e.what() << std::endl;
        }
    }
}

void NetworkKernel::sendMessage(const std::string& target_kernel, const std::string& message) {
    // Преобразуем сообщение в бинарные данные
    std::vector<uint8_t> data(message.begin(), message.end());
    
    // Отправляем сообщение всем подключенным клиентам
    for (const auto& connection : connections_) {
        sendData(connection.first, data);
    }
}

double NetworkKernel::getLoad() const {
    updateMetrics();
    
    // Вычисляем нагрузку на основе количества активных соединений
    // и объема переданных данных
    double connection_load = static_cast<double>(metrics_.active_connections) / 1000.0;
    double data_load = static_cast<double>(metrics_.bytes_sent + metrics_.bytes_received) / (1024 * 1024 * 1024);
    
    return std::min(1.0, connection_load + data_load);
}

void NetworkKernel::setConnectionHandler(ConnectionHandler handler) {
    connection_handler_ = std::move(handler);
}

void NetworkKernel::setDataHandler(DataHandler handler) {
    data_handler_ = std::move(handler);
}

void NetworkKernel::initializeResources() {
    // Инициализация сетевых ресурсов
}

void NetworkKernel::cleanupResources() {
    // Закрываем все соединения
    for (auto& connection : connections_) {
        try {
            connection.second->close();
        } catch (const std::exception& e) {
            std::cerr << "NetworkKernel: Ошибка закрытия соединения: " << e.what() << std::endl;
        }
    }
    connections_.clear();
    
    // Останавливаем акцептор
    if (acceptor_) {
        try {
            acceptor_->close();
        } catch (const std::exception& e) {
            std::cerr << "NetworkKernel: Ошибка закрытия акцептора: " << e.what() << std::endl;
        }
    }
}

void NetworkKernel::acceptConnection() {
    if (!acceptor_) return;

    auto socket = std::make_shared<boost::asio::ip::tcp::socket>(io_context_);
    acceptor_->async_accept(*socket,
        [this, socket](const boost::system::error_code& error) {
            if (!error) {
                std::string connection_id = socket->remote_endpoint().address().to_string() + ":" +
                                         std::to_string(socket->remote_endpoint().port());
                
                connections_[connection_id] = socket;
                if (connection_handler_) {
                    connection_handler_(connection_id, true);
                }
                
                std::cout << "NetworkKernel: Новое подключение от " << connection_id << std::endl;
                handleConnection(connection_id, socket);
            }
            
            // Продолжаем принимать новые соединения
            acceptConnection();
        });
}

void NetworkKernel::handleConnection(const std::string& connection_id,
                                  std::shared_ptr<boost::asio::ip::tcp::socket> socket) {
    auto buffer = std::make_shared<std::vector<uint8_t>>(4096);
    
    socket->async_read_some(boost::asio::buffer(*buffer),
        [this, connection_id, socket, buffer](const boost::system::error_code& error,
                                            std::size_t bytes_transferred) {
            if (!error) {
                metrics_.bytes_received += bytes_transferred;
                
                if (data_handler_) {
                    std::vector<uint8_t> data(buffer->begin(),
                                            buffer->begin() + bytes_transferred);
                    data_handler_(connection_id, data);
                }
                
                // Продолжаем чтение
                handleConnection(connection_id, socket);
            } else {
                // Соединение закрыто или произошла ошибка
                disconnect(connection_id);
            }
        });
}

void NetworkKernel::processTasks() {
    while (!task_queue_.empty()) {
        const auto& task = task_queue_.front();
        sendData(task.target_id, task.data);
        task_queue_.pop();
    }
}

void NetworkKernel::updateMetrics() const {
    auto now = std::chrono::system_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - metrics_.last_update).count();
    
    if (elapsed >= 1) {
        metrics_.active_connections = connections_.size();
        metrics_.last_update = now;
    }
}

} // namespace cloud 