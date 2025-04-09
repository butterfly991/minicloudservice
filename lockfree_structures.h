#pragma once

#include "architecture.h"
#include "advanced_synchronization.h"

#include <vector>
#include <atomic>

namespace core {

    namespace lockfree {

        template<typename T>
        class LockFreeQueue {
        public:
            void enqueue(const T& item);
            T dequeue();
            // Другие методы...
        };
    
        template<typename T>
	    class LockFreeStack {
	    private:
		    struct Node
	    	{
		    	T data;
			    Node* next;
		    };
		    std::atomic<Node*>head_{ nullptr };
	    public:
		    void push(const T& value);
		    bool pop(T& value);
	    };

	    template<typename T>
	    class LockFreeHashMap {
	    private:
		    struct Bucket
		    {
			    LockFreeQueue<T> chain;
		    };
		    std::vector<Bucket>buckets_;
		    std::atimoc<size_t>size_{0};
	    public:
		    explicit LockFreeHashMap{ size_t bucket_count };
		    bool insert(const T& key, const T& value);
		    bool find(const T& key, T& value);	
	    };

    }
}