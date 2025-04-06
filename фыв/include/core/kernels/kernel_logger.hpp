#pragma once

#include <string>
#include <chrono>
#include <memory>
#include <vector>
#include <mutex>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>

namespace cloud {
namespace core {
namespace kernels {

class KernelLogger {
public:
    enum class LogLevel {
        DEBUG,
        INFO,
        WARNING,
        ERROR,
        CRITICAL
    };

    struct LogEntry {
        std::chrono::system_clock::time_point timestamp;
        LogLevel level;
        std::string kernel_name;
        std::string message;
        std::string details;
        std::string stack_trace;
    };

    struct LoggerConfig {
        bool enable_file_logging{true};
        bool enable_console_logging{true};
        bool enable_metrics_logging{true};
        bool enable_diagnostics{true};
        
        std::string log_directory{"logs"};
        std::string log_file_prefix{"kernel"};
        size_t max_log_file_size{10 * 1024 * 1024};  // 10MB
        size_t max_log_files{5};
        
        std::chrono::seconds log_rotation_interval{std::chrono::hours(24)};
        std::chrono::seconds log_retention_period{std::chrono::days(7)};
        
        bool enable_remote_logging{false};
        std::string remote_logging_endpoint;
        std::chrono::milliseconds remote_logging_timeout{std::chrono::seconds(5)};
    };

    explicit KernelLogger(const LoggerConfig& config = LoggerConfig{})
        : config_(config) {
        initialize_logger();
    }

    ~KernelLogger() {
        flush_logs();
    }

    // Основные методы логирования
    void log(LogLevel level, const std::string& kernel_name, 
             const std::string& message, const std::string& details = "") {
        LogEntry entry{
            std::chrono::system_clock::now(),
            level,
            kernel_name,
            message,
            details,
            capture_stack_trace()
        };

        std::lock_guard<std::mutex> lock(log_mutex_);
        log_entries_.push_back(entry);
        
        if (should_flush()) {
            flush_logs();
        }
    }

    // Специализированные методы логирования
    void log_metrics(const std::string& kernel_name, 
                    const BaseKernel::KernelMetrics& metrics) {
        if (!config_.enable_metrics_logging) return;

        std::stringstream ss;
        ss << "Metrics for kernel " << kernel_name << ":\n"
           << "  Total tasks: " << metrics.total_tasks << "\n"
           << "  Completed tasks: " << metrics.completed_tasks << "\n"
           << "  Failed tasks: " << metrics.failed_tasks << "\n"
           << "  CPU usage: " << metrics.cpu_usage << "%\n"
           << "  Memory usage: " << metrics.memory_usage << " bytes\n"
           << "  Throughput: " << metrics.throughput << " tasks/sec\n"
           << "  Error rate: " << metrics.error_rate << "%\n"
           << "  Utilization: " << metrics.utilization << "%";

        log(LogLevel::INFO, kernel_name, "Metrics update", ss.str());
    }

    void log_diagnostic(const std::string& kernel_name,
                       const std::string& diagnostic_type,
                       const std::string& details) {
        if (!config_.enable_diagnostics) return;

        log(LogLevel::DEBUG, kernel_name, 
            "Diagnostic: " + diagnostic_type, details);
    }

    void log_error(const std::string& kernel_name,
                  const std::string& error_message,
                  const std::exception& e) {
        std::stringstream ss;
        ss << "Error: " << error_message << "\n"
           << "Exception: " << e.what() << "\n"
           << "Type: " << typeid(e).name();

        log(LogLevel::ERROR, kernel_name, "Kernel error", ss.str());
    }

    void log_recovery(const std::string& kernel_name,
                     const std::string& recovery_action,
                     bool success) {
        std::string status = success ? "successful" : "failed";
        log(LogLevel::INFO, kernel_name, 
            "Recovery action: " + recovery_action + " (" + status + ")");
    }

    // Управление логами
    void flush_logs() {
        std::lock_guard<std::mutex> lock(log_mutex_);
        
        if (config_.enable_file_logging) {
            write_to_file();
        }
        
        if (config_.enable_console_logging) {
            write_to_console();
        }
        
        if (config_.enable_remote_logging) {
            send_to_remote();
        }
        
        log_entries_.clear();
    }

    // Анализ логов
    std::vector<LogEntry> get_recent_logs(size_t count = 100) const {
        std::lock_guard<std::mutex> lock(log_mutex_);
        size_t start = log_entries_.size() > count ? 
                      log_entries_.size() - count : 0;
        return std::vector<LogEntry>(
            log_entries_.begin() + start, 
            log_entries_.end()
        );
    }

    std::vector<LogEntry> get_logs_by_level(LogLevel level) const {
        std::lock_guard<std::mutex> lock(log_mutex_);
        std::vector<LogEntry> filtered;
        for (const auto& entry : log_entries_) {
            if (entry.level == level) {
                filtered.push_back(entry);
            }
        }
        return filtered;
    }

    // Диагностика
    std::string generate_diagnostic_report(const std::string& kernel_name) const {
        std::stringstream ss;
        ss << "Diagnostic Report for " << kernel_name << "\n"
           << "Generated at: " << get_current_timestamp() << "\n\n"
           << "Recent Errors:\n";
        
        auto errors = get_logs_by_level(LogLevel::ERROR);
        for (const auto& error : errors) {
            ss << "  " << format_log_entry(error) << "\n";
        }
        
        ss << "\nPerformance Issues:\n";
        auto warnings = get_logs_by_level(LogLevel::WARNING);
        for (const auto& warning : warnings) {
            ss << "  " << format_log_entry(warning) << "\n";
        }
        
        return ss.str();
    }

private:
    void initialize_logger() {
        if (config_.enable_file_logging) {
            create_log_directory();
            rotate_log_files();
        }
    }

    void create_log_directory() {
        // TODO: Реализовать создание директории для логов
    }

    void rotate_log_files() {
        // TODO: Реализовать ротацию лог-файлов
    }

    bool should_flush() const {
        return log_entries_.size() >= 1000 || // Максимальное количество записей
               std::chrono::system_clock::now() - last_flush_ >= 
               std::chrono::seconds(5); // Интервал сброса
    }

    void write_to_file() {
        // TODO: Реализовать запись в файл
    }

    void write_to_console() {
        for (const auto& entry : log_entries_) {
            std::cout << format_log_entry(entry) << std::endl;
        }
    }

    void send_to_remote() {
        // TODO: Реализовать отправку на удаленный сервер
    }

    std::string format_log_entry(const LogEntry& entry) const {
        std::stringstream ss;
        ss << get_timestamp(entry.timestamp) << " "
           << get_level_string(entry.level) << " "
           << "[" << entry.kernel_name << "] "
           << entry.message;
        
        if (!entry.details.empty()) {
            ss << "\nDetails: " << entry.details;
        }
        
        if (!entry.stack_trace.empty()) {
            ss << "\nStack trace:\n" << entry.stack_trace;
        }
        
        return ss.str();
    }

    std::string get_timestamp(std::chrono::system_clock::time_point time) const {
        auto time_t = std::chrono::system_clock::to_time_t(time);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }

    std::string get_current_timestamp() const {
        return get_timestamp(std::chrono::system_clock::now());
    }

    std::string get_level_string(LogLevel level) const {
        switch (level) {
            case LogLevel::DEBUG: return "DEBUG";
            case LogLevel::INFO: return "INFO";
            case LogLevel::WARNING: return "WARNING";
            case LogLevel::ERROR: return "ERROR";
            case LogLevel::CRITICAL: return "CRITICAL";
            default: return "UNKNOWN";
        }
    }

    std::string capture_stack_trace() const {
        // TODO: Реализовать захват стека вызовов
        return "";
    }

    LoggerConfig config_;
    std::vector<LogEntry> log_entries_;
    mutable std::mutex log_mutex_;
    std::chrono::system_clock::time_point last_flush_;
};

} // namespace kernels
} // namespace core
} // namespace cloud 