#pragma once

#include "architecture.h"
#include "advanced_synchronization.h"

#include <list>
#include <unordered_map>
#include <numa.h>

namespace core {

    namespace caching {

        template<typename Key, typename Value>
        class AdaptiveCache {
        private:
            struct CacheNode {
                Key key;
                Value value;
                size_t frequency;
                typename std::list<CacheNode>::iterator iterator;
            };

            LockFreeQueue<CacheNode> promotion_queue_;
            std::unordered_map<Key, CacheNode> cache_store_;
            size_t max_size_;
            bool enable_hw_prefetch_;

        public:
            explicit AdaptiveCache(size_t size, bool hw_prefetch = false)
                : max_size_(size), enable_hw_prefetch_(hw_prefetch) {}

            bool get(const Key& key, Value& value) {
                auto it = cache_store_.find(key);
                if (it != cache_store_.end()) {
                    // Обновляем частоту доступа
                    CacheNode& node = it->second;
                    node.frequency++;
                    // Перемещаем узел в конец списка для повышения частоты
                    promotion_queue_.enqueue(node);
                    value = node.value;
                    return true;
                }
                return false; // Ключ не найден
            }

            void put(const Key& key, const Value& value) {
                if (cache_store_.size() >= max_size_) {
                    // Удаляем узел с наименьшей частотой
                    CacheNode least_frequent_node = promotion_queue_.dequeue();
                    cache_store_.erase(least_frequent_node.key);
                }
                // Добавляем новый узел в кэш
                CacheNode new_node{ key, value, 1, {} };
                cache_store_[key] = new_node;
                promotion_queue_.enqueue(new_node);
            }

            bool reconfigure(size_t new_size) {
                if (new_size < cache_store_.size()) {
                    // Если новый размер меньше текущего, необходимо удалить элементы
                    while (cache_store_.size() > new_size) {
                        CacheNode least_frequent_node = promotion_queue_.dequeue();
                        cache_store_.erase(least_frequent_node.key);
                    }
                }
                max_size_ = new_size;
                return true; // Успешно изменен размер
            }

            void configure_numa_allocation(int node_count) {
                // Настройка NUMA-выделения
                if (numa_available() == -1) {
                    throw std::runtime_error("NUMA is not available on this system.");
                }

                // Пример выделения памяти на указанном узле
                for (int i = 0; i < node_count; ++i) {
                    // Выделяем память на узле i
                    void* memory = numa_alloc_onnode(max_size_ * sizeof(CacheNode), i);
                    if (!memory) {
                        throw std::runtime_error("Failed to allocate NUMA memory.");
                    }
                    // Здесь можно добавить логику для использования выделенной памяти
                }
            }
        };

        class NVMBackend {
        private:
            void* pmem_addr_;
            size_t mapped_size_;
        public:
            explicit NVMBackend(const std::string& path);
            void write(const void* data, size_t size, size_t offset);
            void read(void* buffer, size_t size, size_t offset);
        };

    }
    
}
