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
		    bool enable_gpu;//включить использование GPU
		    bool enable_fpga;//включить FPGA
		    bool enable_offloading;//разрешить оффлоудинг задач на ускорители
		    bool use_rdma;//для прямого доступа к памяти 
		    bool enable_jit_compilation;//включение JIT-компиляции 
		    uint32_t gpu_flags;//флаги для инициализации контекста GPU(cudaDeviceMapHost)
		    std::string fpga_bitstream;//путь к битстриму(прошивке) для FPGA
	    };

		class FPGAManagementUnit {
		private:
			struct Impl;
			std::unique_ptr<Impl> impl_;
		public:
			void load_bitstream(const std::string& path);//загрузка прошивки для FPGA
			void reconfigure(const AccelerationPolicy& policy);//перенастройка FPGA используя AccelerationPolicy (включение и отключении опций)
		};

		class GPUMultiContext {
		private:
			cudaStream_t stream_;//CUDA-поток для асинхронных операций 
			int device_count_;//количество GPU
		public:
			explicit GPUMultiContext(int flags = 0);//конструктор для инициализации флагов для CUDA (по умолчанию = 0)
			void synchronize() const;//синхронизация всех операций в потоке (cudaStreamSynchronize)
			void* allocate_pinned(size_t size);//выделение памяти для (cudaHostAlloc) быстрой передачи данных в GPU
		};

		class SmartNICController { // Управление смарт-сетевой картой
		public:
			void configure_offloading(bool enable);// включение и выключение оффлоудинга задач на NIC (обработка сетевых пакетов)
			void set_rdma_mode(bool enable);//включение RDMA
		};

		template<typename T>
		class AcceleratorBuffer { //Управление буфером данных на ускорителе (GPU/FPGA)
		private:
			T* device_ptr_; //указатель на память ускорителя  
			size_t size_;//размер буфера 
		public:
			AcceleratorBuffer(size_t elements);//выделяет память на ускорителе
			void copy_to_device(const T* host_data);//копирует данные с хоста на ускоритель
			void copy_from_host(T* host_dest);//копирует даныые с ускорителя на хост  

		};

    	struct AcceleratorPriority {
       		int priority_level;
   		};
	}
}

