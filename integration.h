#pragma once

#include "architecture.h"

#include <string>
#include <memory>

namespace core {
    
    namespace integration {
        
        class Integrator {
        public:
            void integrate();
            // Другие методы для интеграции...
        };

        class ServiceMesh {
        public:
            void configure(const std::string& config);
            // Другие методы
        };

	    class CloudProviderAdapter {
	    public:
	    	virtual void scale_up(uint32_t node_count) = 0;
	    	virtual void scale_down(uint32_t node_count) = 0;
	    	virtual std::vector<std::string> list_nodes() = 0;
    	};

	    class AWSElasticAdapter : public CloudProviderAdapter {
	    private:
	    	std::string api_key_;
	    public:
	    	explicit AWSElasticAdapter(const std::string& key);
	    	void scale_up(uint32_t node_count) override;
	    	void scale_down(uint32_t node_count) override;
	    	std::vector<std::string>list_nodes() override;
	    };

	    class AutoScaler {
	    private:
	    	std::vector<std::unique_ptr<CloudProviderAdapter>>providers_;
	    	std::atomic<double> load_threshold_{ 0.7 };
	    public:
	    	void add_provider(std::unique_ptr<CloudProviderAdapter> provider);
	    	void evaluate_scaling();
	    	void set_threshold(double threshold) noexcept;
    	};
    
    }

}