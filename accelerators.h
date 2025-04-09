#pragma once

#include "architecture.h"

#include <vector>
#include <memory>
#include <variant>
#include <string>

namespace core {
    
    namespace hardware {

	    enum class AcceleratorType {
		    GPU_CUDA,
		    GPU_OPENCL,
		    FPGA,
		    ASIC,
	    	SMARTNIC
	    };
		

        struct AccelerationPolicy { // настройки аппаратного ускорения
		    bool enable_gpu;
		    bool enable_fpga;
		    bool enable_offloading;
		    bool use_rdma; 
		    bool enable_jit_compilation;
		    uint32_t gpu_flags;
		    std::string fpga_bitstream;//путь к битстриму(прошивке) для FPGA
	    };

		class FPGAManagementUnit {
		private:
			struct Impl;
			std::unique_ptr<Impl> impl_;
		public:
			void load_bitstream(const std::string& path);
			void reconfigure(const AccelerationPolicy& policy);
		};

		class GPUMultiContext {
		private:
			cudaStream_t stream_; 
			int device_count_;
		public:
			explicit GPUMultiContext(int flags = 0);
			void synchronize() const;
			void* allocate_pinned(size_t size);
		};

		class SmartNICController { // Управление смарт-сетевой картой
		public:
			void configure_offloading(bool enable);
			void set_rdma_mode(bool enable);
		};

		template<typename T>
		class AcceleratorBuffer { 
		private:
			T* device_ptr_; 
			size_t size_;
		public:
			AcceleratorBuffer(size_t elements);
			void copy_to_device(const T* host_data);
			void copy_from_host(T* host_dest); 
		};

    	struct AcceleratorPriority {
       		int priority_level;
   		};
	}
}

