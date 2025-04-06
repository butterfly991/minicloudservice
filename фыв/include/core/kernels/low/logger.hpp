#pragma once

#include <string>
#include <iostream>
#include <fstream>
#include <mutex>

namespace cloud {
namespace core {
namespace kernels {
namespace low {

class Logger {
public:
    enum class LogLevel {
        INFO,
        WARNING,
        ERROR,
        DEBUG
    };

    Logger(const std::string& log_file);
    void log(LogLevel level, const std::string& component, const std::string& message);
    void log_error(const std::string& component, const std::string& message);
    void log_info(const std::string& component, const std::string& message);
    void log_warning(const std::string& component, const std::string& message);
    void log_debug(const std::string& component, const std::string& message);

private:
    mutable std::mutex log_mutex_; // Защита логирования
    std::ofstream log_stream_; // Поток для записи в файл
};

} // namespace low
} // namespace kernels
} // namespace core
} // namespace cloud
