#pragma once

#include <functional>
#include <type_traits>
#include <tuple>
#include <memory>
#include <future>
#include <chrono>

namespace cloud {
namespace functional {

// Type-safe function wrapper with error handling
template<typename F>
class SafeFunction {
    F func_;
    std::function<void(const std::exception&)> error_handler_;

public:
    template<typename T>
    SafeFunction(T&& func, std::function<void(const std::exception&)> error_handler = nullptr)
        : func_(std::forward<T>(func))
        , error_handler_(std::move(error_handler)) {}

    template<typename... Args>
    auto operator()(Args&&... args) const {
        try {
            return func_(std::forward<Args>(args)...);
        }
        catch (const std::exception& e) {
            if (error_handler_) {
                error_handler_(e);
            }
            throw;
        }
    }
};

// Memoization wrapper for functions
template<typename F>
class MemoizedFunction {
    F func_;
    std::unordered_map<std::tuple<std::decay_t<Args>...>, 
                      std::invoke_result_t<F, Args...>> cache_;
    mutable std::mutex mutex_;

public:
    template<typename T>
    explicit MemoizedFunction(T&& func) : func_(std::forward<T>(func)) {}

    template<typename... Args>
    auto operator()(Args&&... args) const {
        auto key = std::make_tuple(std::forward<Args>(args)...);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = cache_.find(key);
            if (it != cache_.end()) {
                return it->second;
            }
        }

        auto result = func_(std::forward<Args>(args)...);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            cache_[std::move(key)] = result;
        }
        return result;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_.clear();
    }
};

// Retry wrapper for functions
template<typename F>
class RetryFunction {
    F func_;
    size_t max_retries_;
    std::chrono::milliseconds delay_;
    std::function<bool(const std::exception&)> should_retry_;

public:
    template<typename T>
    RetryFunction(T&& func, size_t max_retries = 3,
                  std::chrono::milliseconds delay = std::chrono::milliseconds(100),
                  std::function<bool(const std::exception&)> should_retry = nullptr)
        : func_(std::forward<T>(func))
        , max_retries_(max_retries)
        , delay_(delay)
        , should_retry_(std::move(should_retry)) {}

    template<typename... Args>
    auto operator()(Args&&... args) const {
        size_t attempts = 0;
        while (true) {
            try {
                return func_(std::forward<Args>(args)...);
            }
            catch (const std::exception& e) {
                if (++attempts >= max_retries_ ||
                    (should_retry_ && !should_retry_(e))) {
                    throw;
                }
                std::this_thread::sleep_for(delay_);
            }
        }
    }
};

// Timeout wrapper for functions
template<typename F>
class TimeoutFunction {
    F func_;
    std::chrono::milliseconds timeout_;

public:
    template<typename T>
    TimeoutFunction(T&& func, std::chrono::milliseconds timeout)
        : func_(std::forward<T>(func))
        , timeout_(timeout) {}

    template<typename... Args>
    auto operator()(Args&&... args) const {
        auto future = std::async(std::launch::async,
            [this, ...args = std::forward<Args>(args)]() mutable {
                return func_(std::forward<Args>(args)...);
            });

        if (future.wait_for(timeout_) == std::future_status::timeout) {
            throw std::runtime_error("Function execution timed out");
        }

        return future.get();
    }
};

// Rate-limited function wrapper
template<typename F>
class RateLimitedFunction {
    F func_;
    std::chrono::milliseconds interval_;
    std::chrono::steady_clock::time_point last_call_;
    mutable std::mutex mutex_;

public:
    template<typename T>
    RateLimitedFunction(T&& func, std::chrono::milliseconds interval)
        : func_(std::forward<T>(func))
        , interval_(interval)
        , last_call_(std::chrono::steady_clock::now() - interval) {}

    template<typename... Args>
    auto operator()(Args&&... args) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_call_).count();
        
        if (elapsed < interval_.count()) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(interval_.count() - elapsed));
        }

        auto result = func_(std::forward<Args>(args)...);
        last_call_ = std::chrono::steady_clock::now();
        return result;
    }
};

// Thread-safe function wrapper with mutex
template<typename F>
class ThreadSafeFunction {
    F func_;
    mutable std::mutex mutex_;

public:
    template<typename T>
    explicit ThreadSafeFunction(T&& func) : func_(std::forward<T>(func)) {}

    template<typename... Args>
    auto operator()(Args&&... args) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return func_(std::forward<Args>(args)...);
    }
};

// Function composition wrapper
template<typename F, typename G>
class ComposedFunction {
    F f_;
    G g_;

public:
    template<typename T, typename U>
    ComposedFunction(T&& f, U&& g)
        : f_(std::forward<T>(f))
        , g_(std::forward<U>(g)) {}

    template<typename... Args>
    auto operator()(Args&&... args) const {
        return f_(g_(std::forward<Args>(args)...));
    }
};

// Helper function for composition
template<typename F, typename G>
auto compose(F&& f, G&& g) {
    return ComposedFunction<std::decay_t<F>, std::decay_t<G>>(
        std::forward<F>(f), std::forward<G>(g));
}

// Function wrapper with logging
template<typename F>
class LoggedFunction {
    F func_;
    std::function<void(const std::string&)> logger_;

public:
    template<typename T>
    LoggedFunction(T&& func, std::function<void(const std::string&)> logger)
        : func_(std::forward<T>(func))
        , logger_(std::move(logger)) {}

    template<typename... Args>
    auto operator()(Args&&... args) const {
        logger_("Function called");
        auto start = std::chrono::steady_clock::now();
        
        try {
            auto result = func_(std::forward<Args>(args)...);
            auto end = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                end - start).count();
            logger_("Function completed in " + std::to_string(duration) + "ms");
            return result;
        }
        catch (const std::exception& e) {
            logger_("Function failed: " + std::string(e.what()));
            throw;
        }
    }
};

} // namespace functional
} // namespace cloud 