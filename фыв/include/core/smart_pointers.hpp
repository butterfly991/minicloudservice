#pragma once

#include <memory>
#include <type_traits>
#include <functional>
#include <utility>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <numa.h>
#include <cuda_runtime.h>

namespace cloud {
namespace memory {

// Custom deleter for OpenSSL types
template<typename T>
struct OpenSSLDeleter {
    void operator()(T* ptr) const {
        if (ptr) {
            if constexpr (std::is_same_v<T, EVP_CIPHER_CTX>) {
                EVP_CIPHER_CTX_free(ptr);
            }
            else if constexpr (std::is_same_v<T, EVP_MD_CTX>) {
                EVP_MD_CTX_free(ptr);
            }
            else if constexpr (std::is_same_v<T, RSA>) {
                RSA_free(ptr);
            }
            else if constexpr (std::is_same_v<T, BIGNUM>) {
                BN_free(ptr);
            }
            else if constexpr (std::is_same_v<T, BIO>) {
                BIO_free(ptr);
            }
        }
    }
};

// Type aliases for OpenSSL smart pointers
using CipherCtxPtr = std::unique_ptr<EVP_CIPHER_CTX, OpenSSLDeleter<EVP_CIPHER_CTX>>;
using MDCtxPtr = std::unique_ptr<EVP_MD_CTX, OpenSSLDeleter<EVP_MD_CTX>>;
using RSAPtr = std::unique_ptr<RSA, OpenSSLDeleter<RSA>>;
using BIGNUMPtr = std::unique_ptr<BIGNUM, OpenSSLDeleter<BIGNUM>>;
using BIOPtr = std::unique_ptr<BIO, OpenSSLDeleter<BIO>>;

// Thread-safe shared pointer with atomic operations
template<typename T>
class AtomicSharedPtr {
    std::atomic<std::shared_ptr<T>> ptr_;

public:
    AtomicSharedPtr() = default;
    explicit AtomicSharedPtr(std::shared_ptr<T> ptr) : ptr_(std::move(ptr)) {}

    void store(std::shared_ptr<T> desired, std::memory_order order = std::memory_order_seq_cst) {
        ptr_.store(std::move(desired), order);
    }

    std::shared_ptr<T> load(std::memory_order order = std::memory_order_seq_cst) const {
        return ptr_.load(order);
    }

    std::shared_ptr<T> exchange(std::shared_ptr<T> desired, 
                               std::memory_order order = std::memory_order_seq_cst) {
        return ptr_.exchange(std::move(desired), order);
    }

    bool compare_exchange_strong(std::shared_ptr<T>& expected, std::shared_ptr<T> desired,
                               std::memory_order success = std::memory_order_seq_cst,
                               std::memory_order failure = std::memory_order_seq_cst) {
        return ptr_.compare_exchange_strong(expected, std::move(desired), success, failure);
    }

    bool compare_exchange_weak(std::shared_ptr<T>& expected, std::shared_ptr<T> desired,
                             std::memory_order success = std::memory_order_seq_cst,
                             std::memory_order failure = std::memory_order_seq_cst) {
        return ptr_.compare_exchange_weak(expected, std::move(desired), success, failure);
    }
};

// RAII wrapper for memory-mapped files
class MemoryMappedFile {
    void* mapped_addr_;
    size_t size_;
    int fd_;

public:
    MemoryMappedFile(const char* filename, size_t size);
    ~MemoryMappedFile();

    MemoryMappedFile(const MemoryMappedFile&) = delete;
    MemoryMappedFile& operator=(const MemoryMappedFile&) = delete;

    void* get_address() const { return mapped_addr_; }
    size_t get_size() const { return size_; }

    template<typename T>
    T* as() {
        return static_cast<T*>(mapped_addr_);
    }
};

// RAII wrapper for aligned memory allocation
template<typename T, size_t Alignment = alignof(T)>
class AlignedPtr {
    T* ptr_;

public:
    explicit AlignedPtr(size_t size) {
        ptr_ = static_cast<T*>(aligned_alloc(Alignment, size));
    }

    ~AlignedPtr() {
        if (ptr_) {
            free(ptr_);
        }
    }

    AlignedPtr(const AlignedPtr&) = delete;
    AlignedPtr& operator=(const AlignedPtr&) = delete;

    T* get() const { return ptr_; }
    T& operator*() const { return *ptr_; }
    T* operator->() const { return ptr_; }
};

// RAII wrapper for CUDA memory
template<typename T>
class CUDAPtr {
    T* ptr_;

public:
    explicit CUDAPtr(size_t size) {
        cudaMalloc(&ptr_, size);
    }

    ~CUDAPtr() {
        if (ptr_) {
            cudaFree(ptr_);
        }
    }

    CUDAPtr(const CUDAPtr&) = delete;
    CUDAPtr& operator=(const CUDAPtr&) = delete;

    T* get() const { return ptr_; }
    T& operator*() const { return *ptr_; }
    T* operator->() const { return ptr_; }
};

// RAII wrapper for NUMA memory
template<typename T>
class NUMAPtr {
    T* ptr_;
    int node_;

public:
    explicit NUMAPtr(size_t size, int node = -1) : node_(node) {
        if (node_ == -1) {
            node_ = numa_node_of_cpu(sched_getcpu());
        }
        ptr_ = static_cast<T*>(numa_alloc_onnode(size, node_));
    }

    ~NUMAPtr() {
        if (ptr_) {
            numa_free(ptr_, sizeof(T));
        }
    }

    NUMAPtr(const NUMAPtr&) = delete;
    NUMAPtr& operator=(const NUMAPtr&) = delete;

    T* get() const { return ptr_; }
    T& operator*() const { return *ptr_; }
    T* operator->() const { return ptr_; }
    int get_node() const { return node_; }
};

// RAII wrapper for pinned memory
template<typename T>
class PinnedPtr {
    T* ptr_;

public:
    explicit PinnedPtr(size_t size) {
        cudaMallocHost(&ptr_, size);
    }

    ~PinnedPtr() {
        if (ptr_) {
            cudaFreeHost(ptr_);
        }
    }

    PinnedPtr(const PinnedPtr&) = delete;
    PinnedPtr& operator=(const PinnedPtr&) = delete;

    T* get() const { return ptr_; }
    T& operator*() const { return *ptr_; }
    T* operator->() const { return ptr_; }
};

// Implementation of MemoryMappedFile
MemoryMappedFile::MemoryMappedFile(const char* filename, size_t size) : size_(size) {
    fd_ = open(filename, O_RDWR | O_CREAT, 0644);
    if (fd_ == -1) {
        throw std::runtime_error("Failed to open file: " + std::string(filename));
    }

    if (ftruncate(fd_, size_) == -1) {
        close(fd_);
        throw std::runtime_error("Failed to truncate file");
    }

    mapped_addr_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (mapped_addr_ == MAP_FAILED) {
        close(fd_);
        throw std::runtime_error("Failed to map memory");
    }
}

MemoryMappedFile::~MemoryMappedFile() {
    if (mapped_addr_ != nullptr) {
        munmap(mapped_addr_, size_);
    }
    if (fd_ != -1) {
        close(fd_);
    }
}

class BlockchainKernel : public KernelBase {
public:
    // Метод для выполнения смарт-контракта
    bool executeSmartContract(const std::string& contract_id, const std::string& method, 
                              const std::vector<std::string>& params) {
        return executeContract(contract_id, method, params);
    }
};

} // namespace memory
} // namespace cloud 