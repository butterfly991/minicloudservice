#pragma once

#include <string>
#include <stdexcept>
#include <memory>
#include <vector>

namespace cloud {

class Error : public std::runtime_error {
public:
    enum class ErrorCode {
        SUCCESS = 0,
        INITIALIZATION_FAILED,
        INVALID_PARAMETER,
        RESOURCE_NOT_FOUND,
        RESOURCE_BUSY,
        OPERATION_TIMEOUT,
        NETWORK_ERROR,
        STORAGE_ERROR,
        THREAD_ERROR,
        KERNEL_ERROR,
        UNKNOWN_ERROR
    };

    struct ErrorContext {
        std::string component;
        std::string operation;
        std::string details;
        std::vector<std::string> stack_trace;
    };

    Error(ErrorCode code, const std::string& message);
    Error(ErrorCode code, const std::string& message, const ErrorContext& context);

    ErrorCode getCode() const { return code_; }
    const ErrorContext& getContext() const { return context_; }

    static std::string getErrorString(ErrorCode code);
    static ErrorContext createContext(const std::string& component,
                                    const std::string& operation,
                                    const std::string& details = "");

private:
    ErrorCode code_;
    ErrorContext context_;
};

class ErrorHandler {
public:
    static void handleError(const Error& error);
    static void setErrorCallback(std::function<void(const Error&)> callback);
    static void clearErrorCallback();

private:
    static std::function<void(const Error&)> error_callback_;
};

} // namespace cloud 