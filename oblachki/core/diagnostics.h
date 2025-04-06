#pragma once

#include "architecture.h"
#include "advanced_synchronization.h"

#include <unordered_map>
#include <variant>
#include <memory>

namespace core {
    
    namespace diagnostics {
       
        enum class LogLevel {
            Debug,
            Info,
            Warning,
            Error,
            Critical
        };

        struct PerformanceMetric {
            std::string name;
            double value;
            std::unordered_map<std::string,std::string> tags;
        };

        class  DiagnosticProbe {
        private:
            HierarchicalLock lock_;
		    std::atomic<bool>_enable{false};
        public:
            void start_sampling();
            void stop_sampling();
            std::vector<PerformanceMetric>get_metrics();
        };
        
        class FaultInjector {
        public:
            enum class FaultType {
			    MemoryLeak,
			    NetworkLatency,
			    CPUStress
		    };
            void inject(FaultType type,uint32_t duration_ms);
		    void reset();    
        };
        
        class ElasticsearchSink {
        private:
            struct Impl;
            std::unique_ptr<Impl> impl_;
        public:
            void emit(const PerformanceMetric& metric);
		    void flush();
	    };

        enum class FailureType {
            HARDWARE_ERROR,
            NETWORK_ERROR,
            SECURITY_ERROR
        };
            
        class ITelemetrySink {
        public:
            virtual void start() = 0;
            virtual void log_failure(FailureType type) = 0;
            // Другие методы...
        };
    
    }
}
