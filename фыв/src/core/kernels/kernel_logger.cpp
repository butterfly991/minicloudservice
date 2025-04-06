#include "core/kernels/kernel_logger.hpp"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <thread>
#include <execinfo.h>
#include <cxxabi.h>
#include <curl/curl.h>
#include <sstream>
#include <mutex>
#include <iomanip>

namespace cloud {
namespace core {
namespace kernels {

void KernelLogger::create_log_directory() {
    try {
        std::filesystem::create_directories(config_.log_directory);
    }
    catch (const std::exception& e) {
        std::cerr << "Failed to create log directory: " << e.what() << std::endl;
    }
}

void KernelLogger::rotate_log_files() {
    try {
        auto now = std::chrono::system_clock::now();
        auto log_path = std::filesystem::path(config_.log_directory);
        
        // Удаляем старые файлы
        for (const auto& entry : std::filesystem::directory_iterator(log_path)) {
            if (entry.path().extension() == ".log") {
                auto file_time = std::filesystem::last_write_time(entry.path());
                auto file_time_point = std::filesystem::file_time_type::clock::to_time_t(file_time);
                auto file_age = std::chrono::system_clock::from_time_t(file_time_point);
                
                if (now - file_age > config_.log_retention_period) {
                    std::filesystem::remove(entry.path());
                }
            }
        }
        
        // Проверяем размер текущего файла
        auto current_log = log_path / (config_.log_file_prefix + ".log");
        if (std::filesystem::exists(current_log)) {
            auto file_size = std::filesystem::file_size(current_log);
            if (file_size >= config_.max_log_file_size) {
                // Создаем новый файл с временной меткой
                auto timestamp = get_current_timestamp();
                std::replace(timestamp.begin(), timestamp.end(), ':', '-');
                auto new_log = log_path / (config_.log_file_prefix + "_" + timestamp + ".log");
                
                std::filesystem::rename(current_log, new_log);
            }
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Failed to rotate log files: " << e.what() << std::endl;
    }
}

void KernelLogger::write_to_file() {
    try {
        auto log_path = std::filesystem::path(config_.log_directory) / 
                       (config_.log_file_prefix + ".log");
        
        std::ofstream log_file(log_path, std::ios::app);
        if (!log_file.is_open()) {
            std::cerr << "Failed to open log file: " << log_path << std::endl;
            return;
        }
        
        for (const auto& entry : log_entries_) {
            log_file << format_log_entry(entry) << std::endl;
        }
        
        log_file.close();
    }
    catch (const std::exception& e) {
        std::cerr << "Failed to write to log file: " << e.what() << std::endl;
    }
}

void KernelLogger::send_to_remote() {
    if (!config_.enable_remote_logging || config_.remote_logging_endpoint.empty()) {
        return;
    }

    // Подготовка данных для отправки
    std::ostringstream oss;
    oss << "logs=" << get_logs(); // Предполагается, что get_logs() возвращает строку с логами

    CURL* curl;
    CURLcode res;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, config_.remote_logging_endpoint.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, oss.str().c_str());

        // Установка заголовков, если необходимо
        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // Отправка запроса
        res = curl_easy_perform(curl);
        
        // Проверка на ошибки
        if(res != CURLE_OK) {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
            // Реализация повторных попыток
            for (int i = 0; i < config_.max_retry_attempts; ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(config_.retry_delay.count()));
                res = curl_easy_perform(curl);
                if (res == CURLE_OK) {
                    break; // Успех, выходим из цикла
                }
            }
        }

        // Освобождение ресурсов
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
}

std::string KernelLogger::get_logs() const {
    std::lock_guard<std::mutex> lock(log_mutex_); // Защита доступа к логам
    std::ostringstream oss;

    for (const auto& entry : log_entries_) {
        oss << format_log_entry(entry) << std::endl; // Предполагается, что format_log_entry форматирует запись лога
    }

    return oss.str(); // Возвращаем все логи в виде строки
}

std::string KernelLogger::format_log_entry(const LogEntry& entry) const {
    // Форматируем запись лога в строку
    std::ostringstream oss;
    oss << "[" << get_current_time() << "] [" << entry.level << "] " << entry.message;
    return oss.str();
}

std::string KernelLogger::get_current_time() const {
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm now_tm = *std::localtime(&now_time);
    std::ostringstream oss;
    oss << std::put_time(&now_tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

std::string KernelLogger::capture_stack_trace() const {
    const int MAX_FRAMES = 32;
    void* callstack[MAX_FRAMES];
    size_t frames = backtrace(callstack, MAX_FRAMES);
    char** symbols = backtrace_symbols(callstack, frames);
    
    if (!symbols) {
        return "Failed to capture stack trace";
    }
    
    std::stringstream ss;
    for (size_t i = 0; i < frames; ++i) {
        std::string symbol = symbols[i];
        
        // Деманглинг C++ символов
        size_t start = symbol.find('(');
        size_t end = symbol.find('+');
        if (start != std::string::npos && end != std::string::npos) {
            std::string mangled = symbol.substr(start + 1, end - start - 1);
            int status;
            char* demangled = abi::__cxa_demangle(mangled.c_str(), nullptr, nullptr, &status);
            if (status == 0 && demangled) {
                symbol.replace(start + 1, end - start - 1, demangled);
                free(demangled);
            }
        }
        
        ss << "  " << symbol << std::endl;
    }
    
    free(symbols);
    return ss.str();
}

} // namespace kernels
} // namespace core
} // namespace cloud 