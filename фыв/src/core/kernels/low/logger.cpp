#include "logger.hpp"
#include <iostream>

namespace cloud {
namespace core {
namespace kernels {
namespace low {

void Logger::log(LogLevel level, const std::string& component, const std::string& message) {
    std::string level_str;
    switch (level) {
        case LogLevel::INFO: level_str = "INFO"; break;
        case LogLevel::WARNING: level_str = "WARNING"; break;
        case LogLevel::ERROR: level_str = "ERROR"; break;
        case LogLevel::DEBUG: level_str = "DEBUG"; break;
    }
    std::cout << "[" << level_str << "] [" << component << "] " << message << std::endl;
}

void Logger::log_error(const std::string& component, const std::string& message) {
    log(LogLevel::ERROR, component, message);
}

void Logger::log_info(const std::string& component, const std::string& message) {
    log(LogLevel::INFO, component, message);
}

void Logger::log_warning(const std::string& component, const std::string& message) {
    log(LogLevel::WARNING, component, message);
}

void Logger::log_debug(const std::string& component, const std::string& message) {
    log(LogLevel::DEBUG, component, message);
}

} // namespace low
} // namespace kernels
} // namespace core
} // namespace cloud
