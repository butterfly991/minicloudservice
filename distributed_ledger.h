// diagnostics.h
#pragma once

#ifndef CORE_DIAGNOSTICS_H
#define CORE_DIAGNOSTICS_H

#include "architecture.h"
#include "advanced_synchronization.h"
#include "integration.h"

#include <memory>
#include <atomic>
#include <vector>
#include <unordered_map>
#include <string>
#include <string_view>
#include <chrono>
#include <functional>
#include <span>
#include <variant>
#include <cstring>
#include <stdexcept>
#include <iostream>
#include <iomanip>
#include <curl/curl.h>
#include <sstream>

// Auto-detect compatibility mode
#ifndef CORE_ENGINE_COMPAT_MODE
#define CORE_ENGINE_COMPAT_MODE 1
#endif

// Compiler-specific optimizations
#if defined(_MSC_VER)
#define DIAG_FORCE_INLINE __forceinline
#define DIAG_NORETURN __declspec(noreturn)
#else
#define DIAG_FORCE_INLINE __attribute__((always_inline)) inline
#define DIAG_NORETURN __attribute__((noreturn))
#endif

namespace core {
namespace diagnostics {

// Constants
constexpr size_t MAX_METRIC_NAME_LENGTH = 128;
constexpr size_t MAX_TAG_KEY_LENGTH = 32;
constexpr size_t MAX_TAG_VALUE_LENGTH = 64;
constexpr size_t MAX_TAG_COUNT = 16;
constexpr std::chrono::milliseconds DEFAULT_FLUSH_INTERVAL{500};

// Type Aliases
using Timestamp = std::chrono::high_resolution_clock::time_point;
using MetricValue = std::variant<double, int64_t, uint64_t, std::string_view>;

// Enumerations
enum class LogLevel : uint8_t {
    Debug, Info, Warning, Error, Critical, Fatal
};

enum class FailureType : uint8_t {
    HardwareError, NetworkError, SecurityViolation,
    DataCorruption, ResourceExhaustion, PerformanceDegradation
};

enum class SamplingMode : uint8_t {
    FixedRate, Adaptive, OnDemand
};

// Data Structures
#pragma pack(push, 1)
struct MetricTag {
    char key[MAX_TAG_KEY_LENGTH] = {};
    char value[MAX_TAG_VALUE_LENGTH] = {};
};

struct CompactMetric {
    char name[MAX_METRIC_NAME_LENGTH] = {};
    MetricValue value;
    Timestamp timestamp;
    MetricTag tags[MAX_TAG_COUNT] = {};
    uint8_t tag_count = 0;
};
#pragma pack(pop)

// Forward Declarations
class ITelemetrySink;
class DiagnosticProbe;
class FaultInjector;

// String Safety
namespace detail {
    template<size_t N>
    DIAG_FORCE_INLINE void strcopy(char (&dest)[N], std::string_view src) noexcept {
        const size_t copy_len = std::min(src.size(), N-1);
        std::memcpy(dest, src.data(), copy_len);
        dest[copy_len] = '\0';
    }
}

// Interface Implementations
class ITelemetrySink {
public:
    virtual ~ITelemetrySink() = default;

    // Core Interface
    virtual void start() = 0;
    virtual void stop() noexcept = 0;
    virtual void flush() = 0;
    
    virtual void log(LogLevel level, std::string_view message) noexcept = 0;
    virtual void log_failure(FailureType type, std::string_view context) noexcept = 0;
    virtual void push_metric(const CompactMetric& metric) noexcept = 0;
    virtual void push_metrics_batch(std::span<const CompactMetric> metrics) noexcept = 0;

    // Compatibility Layer
#if CORE_ENGINE_COMPAT_MODE
    struct PerformanceMetric {
        std::string name;
        double value;
        std::unordered_map<std::string, std::string> tags;
        Timestamp timestamp;
    };

    void push_legacy_metric(const PerformanceMetric& metric) noexcept {
        CompactMetric cm{};
        detail::strcopy(cm.name, metric.name);
        cm.value = metric.value;
        cm.timestamp = metric.timestamp;
        
        cm.tag_count = 0;
        for (const auto& [k, v] : metric.tags) {
            if (cm.tag_count >= MAX_TAG_COUNT) break;
            detail::strcopy(cm.tags[cm.tag_count].key, k);
            detail::strcopy(cm.tags[cm.tag_count].value, v);
            ++cm.tag_count;
        }
        push_metric(cm);
    }
#endif
};

// Diagnostic Probe Implementation
class DiagnosticProbe {
public:
    explicit DiagnosticProbe(ITelemetrySink& sink) : sink_(sink) {}

    DIAG_FORCE_INLINE void record_metric(const CompactMetric& metric) noexcept {
        if (sampling_enabled_) {
            buffer_.push(metric);
        }
    }

    void start_sampling() noexcept { sampling_enabled_.store(true); }
    void stop_sampling() noexcept { sampling_enabled_.store(false); }

    void flush_metrics() {
        CompactMetric metric;
        while (buffer_.pop(metric)) {
            sink_.push_metric(metric);
        }
    }

private:
    ITelemetrySink& sink_;
    std::atomic<bool> sampling_enabled_{false};
    LockFreeRingBuffer<CompactMetric, 1024> buffer_;
};

// Fault Injector Implementation
class FaultInjector {
public:
    struct FaultProfile {
        std::string name;
        std::function<void()> injector;
        std::function<void()> cleaner;
    };

    explicit FaultInjector(ITelemetrySink& sink) : sink_(sink) {}

    void inject(std::string_view name, std::chrono::milliseconds duration) {
        if (auto it = profiles_.find(name.data()); it != profiles_.end()) {
            current_profile_ = &it->second;
            current_profile_->injector();
            schedule_cleanup(duration);
        }
    }

    void reset() noexcept {
        if (current_profile_) {
            current_profile_->cleaner();
            current_profile_ = nullptr;
        }
    }

    void register_profile(FaultProfile profile) {
        profiles_.emplace(profile.name, std::move(profile));
    }

private:
    void schedule_cleanup(std::chrono::milliseconds delay) {
        cleanup_timer_ = std::make_unique<Timer>([this] {
            reset();
        });
        cleanup_timer_->start(delay);
    }

    ITelemetrySink& sink_;
    std::unordered_map<std::string, FaultProfile> profiles_;
    const FaultProfile* current_profile_ = nullptr;
    
    struct Timer {
        std::function<void()> callback;
        std::atomic<bool> running{false};

        void start(std::chrono::milliseconds delay) {
            running.store(true);
            std::thread([this, delay] {
                std::this_thread::sleep_for(delay);
                if (running.exchange(false)) {
                    callback();
                }
            }).detach();
        }

        ~Timer() { running.store(false); }
    };
    std::unique_ptr<Timer> cleanup_timer_;
};

// Elasticsearch Sink Implementation
class ElasticsearchSink : public ITelemetrySink {
public:
    explicit ElasticsearchSink(std::string_view endpoint)
        : endpoint_(endpoint) {}

    void start() override {
        worker_running_.store(true);
        worker_thread_ = std::thread(&ElasticsearchSink::process_queue, this);
    }

    void stop() noexcept override {
        worker_running_.store(false);
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }

    void flush() override {
        std::lock_guard lock(queue_mutex_);
        while (!metric_queue_.empty()) {
            send_to_elastic(metric_queue_.front());
            metric_queue_.pop();
        }
    }

    void log(LogLevel level, std::string_view message) noexcept override {
        std::lock_guard lock(queue_mutex_);
        log_queue_.emplace(level, std::string(message));
    }

    void push_metric(const CompactMetric& metric) noexcept override {
        std::lock_guard lock(queue_mutex_);
        metric_queue_.push(metric);
    }

private:
    void process_queue() {
        while (worker_running_.load()) {
            std::this_thread::sleep_for(DEFAULT_FLUSH_INTERVAL);
            flush();
        }
    }

	void send_to_elastic(const CompactMetric& metric) {
		CURL* curl = curl_easy_init();
		if (!curl) {
			std::cerr << "Failed to initialize CURL\n";
			return;
		}
	
		// Формируем JSON-документ
		std::string json;
		{
			std::stringstream ss;
			ss << "{"
			   << "\"@timestamp\":\"" << std::chrono::duration_cast<std::chrono::milliseconds>(
				   metric.timestamp.time_since_epoch()).count() << "\","
			   << "\"name\":\"" << metric.name << "\","
			   << "\"value\":";
	
			std::visit([&](auto&& arg) {
				using T = std::decay_t<decltype(arg)>;
				if constexpr (std::is_same_v<T, std::string_view>) {
					ss << "\"" << arg << "\"";
				} else {
					ss << arg;
				}
			}, metric.value);
	
			ss << ",\"tags\":{";
			for (uint8_t i = 0; i < metric.tag_count; ++i) {
				if (i > 0) ss << ",";
				ss << "\"" << metric.tags[i].key << "\":\"" 
				   << metric.tags[i].value << "\"";
			}
			ss << "}}";
			json = ss.str();
		}
	
		// Настраиваем запрос
		struct curl_slist* headers = nullptr;
		headers = curl_slist_append(headers, "Content-Type: application/json");
		
		curl_easy_setopt(curl, CURLOPT_URL, (endpoint_ + "/_doc").c_str());
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json.c_str());
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, json.size());
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
		curl_easy_setopt(curl, CURLOPT_USERAGENT, "CoreDiagnostics/1.0");
		curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 500L);
	
		// Выполняем запрос
		CURLcode res = curl_easy_perform(curl);
		if (res != CURLE_OK) {
			std::cerr << "Elasticsearch request failed: "
					  << curl_easy_strerror(res) << "\n";
		}
	
		// Очистка ресурсов
		curl_slist_free_all(headers);
		curl_easy_cleanup(curl);
	}

    std::string endpoint_;
    std::atomic<bool> worker_running_{false};
    std::thread worker_thread_;
    
    std::mutex queue_mutex_;
    std::queue<CompactMetric> metric_queue_;
    std::queue<std::pair<LogLevel, std::string>> log_queue_;
};

// Utility Functions
namespace utils {
    DIAG_FORCE_INLINE Timestamp current_timestamp() noexcept {
        return std::chrono::high_resolution_clock::now();
    }

    DIAG_FORCE_INLINE std::string serialize_metric(const CompactMetric& metric) {
        std::stringstream ss;
        ss << "Metric{name=" << metric.name 
           << ", value=" << std::visit([](auto&& v){ return std::to_string(v); }, metric.value)
           << ", tags=[";
        for (uint8_t i = 0; i < metric.tag_count; ++i) {
            ss << metric.tags[i].key << "=" << metric.tags[i].value;
            if (i < metric.tag_count - 1) ss << ", ";
        }
        ss << "]}";
        return ss.str();
    }
}

} // namespace diagnostics
} // namespace core

#endif // CORE_DIAGNOSTICS_H