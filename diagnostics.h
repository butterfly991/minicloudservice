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
#include <string_view>
#include <chrono>
#include <functional>
#include <span>
#include <variant>

// Макрос для форсирования inline в релизе
#if defined(_MSC_VER)
#define DIAG_FORCE_INLINE __forceinline
#else
#define DIAG_FORCE_INLINE __attribute__((always_inline)) inline
#endif

namespace core {
namespace diagnostics {

// =============================================
// Типы и константы
// =============================================
using Timestamp = std::chrono::time_point<std::chrono::high_resolution_clock>;
using MetricValue = std::variant<double, int64_t, uint64_t, std::string_view>;

constexpr size_t MAX_METRIC_NAME_LENGTH = 128;
constexpr size_t MAX_TAG_COUNT = 16;
constexpr std::chrono::milliseconds DEFAULT_FLUSH_INTERVAL{500};

// =============================================
// Перечисления
// =============================================
enum class LogLevel : uint8_t {
    Debug,
    Info,
    Warning,
    Error,
    Critical,
    Fatal
};

enum class FailureType : uint8_t {
    HardwareError,
    NetworkError,
    SecurityViolation,
    DataCorruption,
    ResourceExhaustion,
    PerformanceDegradation
};

enum class SamplingMode : uint8_t {
    FixedRate,
    Adaptive,
    OnDemand
};

// =============================================
// Структуры данных
// =============================================
#pragma pack(push, 1)
struct MetricTag {
    char key[32];
    char value[64];
};

struct CompactMetric {
    char name[MAX_METRIC_NAME_LENGTH];
    MetricValue value;
    Timestamp timestamp;
    MetricTag tags[MAX_TAG_COUNT];
    uint8_t tag_count;
};
#pragma pack(pop)

// =============================================
// Интерфейсы
// =============================================
class ITelemetrySink {
public:
    using Ptr = std::unique_ptr<ITelemetrySink>;
    using Callback = std::function<void(const std::string&)>;
    
    virtual ~ITelemetrySink() noexcept = default;

    // Управление жизненным циклом
    virtual void start() = 0;
    virtual void stop() noexcept = 0;
    virtual void flush() = 0;

    // Логирование
    virtual void log(LogLevel level, std::string_view message) noexcept = 0;
    virtual void log_failure(FailureType type, std::string_view context) noexcept = 0;

    // Метрики
    virtual void push_metric(const CompactMetric& metric) noexcept = 0;
    virtual void push_metrics_batch(std::span<const CompactMetric> metrics) noexcept = 0;

    // Конфигурация
    virtual void set_sampling_rate(uint32_t samples_per_sec) noexcept = 0;
    virtual void set_sampling_mode(SamplingMode mode) noexcept = 0;
    virtual void set_error_handler(Callback&& handler) noexcept = 0;
};

// =============================================
// Основные классы
// =============================================
class DiagnosticProbe final {
public:
    using MetricCallback = std::function<void(const CompactMetric&)>;

    explicit DiagnosticProbe(ITelemetrySink& sink) noexcept;
    ~DiagnosticProbe() noexcept;

    // Не копируемый, только перемещаемый
    DiagnosticProbe(const DiagnosticProbe&) = delete;
    DiagnosticProbe& operator=(const DiagnosticProbe&) = delete;
    DiagnosticProbe(DiagnosticProbe&&) noexcept;
    DiagnosticProbe& operator=(DiagnosticProbe&&) noexcept;

    // Управление сбором метрик
    void start_sampling() noexcept;
    void stop_sampling() noexcept;
    void set_sampling_interval(std::chrono::milliseconds interval) noexcept;

    // Регистрация метрик
    DIAG_FORCE_INLINE void record_metric(const CompactMetric& metric) noexcept {
        if (is_sampling_.load(std::memory_order_acquire)) {
            buffer_.push(metric);
        }
    }

    // Подписки
    void subscribe(MetricCallback&& callback) noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    std::atomic<bool> is_sampling_{false};
    LockFreeRingBuffer<CompactMetric> buffer_{1024};
};

class FaultInjector final {
public:
    struct FaultProfile {
        std::string name;
        std::function<void()> injector;
        std::function<void()> cleaner;
    };

    explicit FaultInjector(ITelemetrySink& sink) noexcept;
    ~FaultInjector() noexcept;

    void inject(std::string_view fault_name, std::chrono::milliseconds duration) noexcept;
    void reset() noexcept;
    void register_fault_profile(FaultProfile&& profile) noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

// =============================================
// Реализации Sink'ов
// =============================================
class ElasticsearchSink final : public ITelemetrySink {
public:
    explicit ElasticsearchSink(const NetworkEndpoint& endpoint);
    ~ElasticsearchSink() override;

    // ITelemetrySink implementation
    void start() override;
    void stop() noexcept override;
    void flush() override;
    void log(LogLevel level, std::string_view message) noexcept override;
    void log_failure(FailureType type, std::string_view context) noexcept override;
    void push_metric(const CompactMetric& metric) noexcept override;
    void push_metrics_batch(std::span<const CompactMetric> metrics) noexcept override;
    void set_sampling_rate(uint32_t samples_per_sec) noexcept override;
    void set_sampling_mode(SamplingMode mode) noexcept override;
    void set_error_handler(Callback&& handler) noexcept override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

class PrometheusSink final : public ITelemetrySink {
    // Аналогичная реализация для Prometheus
};

// =============================================
// Вспомогательные функции
// =============================================
namespace utils {

[[nodiscard]] std::string serialize_metric(const CompactMetric& metric) noexcept;
[[nodiscard]] Timestamp current_timestamp() noexcept;
[[nodiscard]] const char* failure_type_to_string(FailureType type) noexcept;

} // namespace utils

} // namespace diagnostics
} // namespace core

// =============================================
// Inline реализации
// =============================================
namespace core {
namespace diagnostics {

inline DiagnosticProbe::DiagnosticProbe(DiagnosticProbe&& other) noexcept 
    : impl_(std::move(other.impl_)), 
      is_sampling_(other.is_sampling_.load()),
      buffer_(std::move(other.buffer_)) {}

inline void DiagnosticProbe::start_sampling() noexcept {
    is_sampling_.store(true, std::memory_order_release);
    if (impl_) impl_->start_worker();
}

} // namespace diagnostics
} // namespace core

#endif // CORE_DIAGNOSTICS_H