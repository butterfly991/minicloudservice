#include "core/error.hpp"
#include <sstream>
#include <iostream>

namespace cloud {

std::function<void(const Error&)> ErrorHandler::error_callback_;

Error::Error(ErrorCode code, const std::string& message)
    : std::runtime_error(message)
    , code_(code)
    , context_() {
}

Error::Error(ErrorCode code, const std::string& message, const ErrorContext& context)
    : std::runtime_error(message)
    , code_(code)
    , context_(context) {
}

std::string Error::getErrorString(ErrorCode code) {
    switch (code) {
        case ErrorCode::SUCCESS:
            return "Success";
        case ErrorCode::INITIALIZATION_FAILED:
            return "Initialization failed";
        case ErrorCode::INVALID_PARAMETER:
            return "Invalid parameter";
        case ErrorCode::RESOURCE_NOT_FOUND:
            return "Resource not found";
        case ErrorCode::RESOURCE_BUSY:
            return "Resource is busy";
        case ErrorCode::OPERATION_TIMEOUT:
            return "Operation timeout";
        case ErrorCode::NETWORK_ERROR:
            return "Network error";
        case ErrorCode::STORAGE_ERROR:
            return "Storage error";
        case ErrorCode::THREAD_ERROR:
            return "Thread error";
        case ErrorCode::KERNEL_ERROR:
            return "Kernel error";
        case ErrorCode::UNKNOWN_ERROR:
            return "Unknown error";
        default:
            return "Undefined error";
    }
}

Error::ErrorContext Error::createContext(const std::string& component,
                                      const std::string& operation,
                                      const std::string& details) {
    ErrorContext context;
    context.component = component;
    context.operation = operation;
    context.details = details;
    return context;
}

void ErrorHandler::handleError(const Error& error) {
    std::stringstream ss;
    ss << "Error in " << error.getContext().component 
       << " during " << error.getContext().operation << ": "
       << error.what() << "\n";
    
    if (!error.getContext().details.empty()) {
        ss << "Details: " << error.getContext().details << "\n";
    }
    
    if (!error.getContext().stack_trace.empty()) {
        ss << "Stack trace:\n";
        for (const auto& frame : error.getContext().stack_trace) {
            ss << "  " << frame << "\n";
        }
    }
    
    std::cerr << ss.str();
    
    if (error_callback_) {
        error_callback_(error);
    }
}

void ErrorHandler::setErrorCallback(std::function<void(const Error&)> callback) {
    error_callback_ = std::move(callback);
}

void ErrorHandler::clearErrorCallback() {
    error_callback_ = nullptr;
}

} // namespace cloud 