#pragma once

#include"architecture.h"

#include <atomic>
#include <thread>

namespace core {
    
    namespace concurrency {
        
        class HierarchicalLock {
        private:
		    std::atomic<uint64_t> lock_;
		    static thread_local uint64_t current_hierarchy;
	    public:
		    void lock(uint64_t hierarchy);
		    void unlock();
	    };

	    template<typename T>
	    class LockFreeQueue {
	    private:
		    struct Node {
			    T data;
			    std::atomic<Node*> next;
		    };
		    std::atomic<Node*>head_;
		    std::atomic<Node*>tail_;
	    public:
		    void enqueue(T item);
		    bool dequeue(T& result);
	    };

        class RCUGuard {
        private:
	    	static thread_local uint64_t version_;
	    public:
		    RCUGuard();
		    ~RCUGuard();
		    static void synchronize();
	    };

        struct LockParams {
            int max_lock_time;
			bool mutex_spin_on_owner;
			
            // Другие параметры
        };
    
    }
}
