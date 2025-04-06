#pragma once

#include "architecture.h"

#include <string>
#include <vector>
#include <memory>

namespace core{
    
    namespace tracing {
    
    	struct TraceSpan {
	    	std::string name;
	    	uint64_t start_time;
		    uint64_t end_time;
	    	std::vector<std::pair<std::string, std::string>> attributes;
	    };

        class DistributedTracer {
        private:
	        struct Context {
		        std::string trace_id;
	    	    std::string span_id;
	        };
    	    thread_local static Context current_context_;
        public:
            class Span {
	        private:
		        DistributedTracer& tracer_;
		        std::string name_;
		        uint64_t start_;
	        public:
		        Span(DistributedTracer& tracer, std::string name);
		        ~Span();
	        };

	        void export_to_jaeger(const TraceSpan& span);
	        static void set_current_context(const Context& ctx);
            void start_trace(); 
            // Другие методы
        };

        class MetricsCollector {
        private:
        	std::atomic<uint64_t> request_counter_{ 0 };
	        std::atomic<uint64_t> error_counter_{ 0 };
        public:
        	void increment_request() noexcept;
	        void increment_error() noexcept;
	        std::pair<uint64_t, uint64_t> snapshot()const noexcept;
        };
    
    }

}


