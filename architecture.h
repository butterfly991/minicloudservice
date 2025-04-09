#pragma once

#include <cstddef>
#include <cstdint>

#include <atomic>
#include <vector>
#include <string>

// выравнивание для кеш-линий
#if defined(__GNUC__) || defined(__clang__) // макросы для GCC & Clang
#define CORE_ALIGN(N) __attribute__((aligned(N)))
#else
#define CORE_ALIGN(N) __declspec(align(N)) // макрос для MSVC
#endif
namespace core {
    namespace architecture {
   
	constexpr size_t CACHE_LINE_SIZE = 64; // кэш-линия для предотвращения false  sharing/ложного разделения 
	constexpr size_t PAGE_SIZE = 4096; // страница памяти
    
    enum class Architecture { // поддерживаемые процессорные архитектуры
		x86_64, 
		ARM_NEON,
		POWER9,
		RISCV
	};
    
	// определяем целевую архитектуру процессора и поддержку векторных инструкций
#if defined(__AVX512F__)// макрос для x86-64 с AVX-512 (Foundation Instructions)
	constexpr Architecture TARGET_ARCH = Architecture::x86_64;
	constexpr bool VECTOR_512_SUPPORT = true;
#elif defined(__ARM_NEON)// макрос для ARM с NEON (ARM's SIMD-инструкции) SIMD - параллельная обработка нескольких данных одной инструкцией
	constexpr Architecture TARGET_ARCH = Architecture::ARM_NEON;
	constexpr bool VECTOR_128_SUPPORT = true;
#endif

    struct MemoryLayout {
		static constexpr size_t STACK_SIZE = 2 * 1024 * 1024; //  Каждому потоку выделяется стек размером 2 МБ стек (можно менять от ситуации)
		static constexpr size_t HEAP_ALIGNMENT = 64;//выравнивание кучи,сетевые буферы,выравнивание для работы с векторами SIMD
	};

    // Определения архитектурных параметров и политик
    struct NetworkPolicy {
        bool enable_rdma;
        // Другие параметры...
    };
    
    template<typename T>
	struct HardwareAtomic
	{
		std::atomic<T> value;
		T load_acquire()const noexcept {
			return value.load(std::memory_order_acquire);//Запрещает компилятору/процессору перемещать последующие операции чтения/записи до этой загрузки.
		}

		void storage_release(T desired)noexcept {
			value.store(desired, std::memory_order_release);//Запрещает компилятору/процессору перемещать предыдущие операции чтения/записи после этой записи
		}
	};

    struct AccelerationPolicy {
        // Параметры для аппаратного ускорения
        bool enable_gpu;
        // Другие параметры...
    };

    namespace hardware {
        enum class AcceleratorType {
            CPU,
            GPU,
            FPGA
        };

        struct AccelerationPolicy {
            bool enable_acceleration;
            AcceleratorType type;
            // Другие параметры
        };
    }
    }
}