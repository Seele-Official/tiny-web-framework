#pragma once
#include <atomic>
#include <cstddef>

namespace seele::structs {
    template<typename T, size_t N = 64>
    class object_pool {
    public:        
        struct node_t {
            alignas(T) std::byte storage[sizeof(T)];
            std::atomic<node_t*> next;
        };

        object_pool() {
            for (size_t i = 0; i < N; ++i) {
                data[i].next.store(i + 1 <= N ? &data[i + 1] : nullptr);
            }
            head.store(&data[0]);
            tail.store(&data[N]);
        }

        template<typename... args_t>
        T* acquire(args_t&&... args);

        void release(T* obj);

    private:
        node_t data[N+1];
        alignas(64) std::atomic<node_t*> head;
        alignas(64) std::atomic<node_t*> tail;
    };

    template<typename T, size_t N>
    template<typename... args_t>
    T* object_pool<T, N>::acquire(args_t&&... args) {
        while (true) {
            node_t* dummy = this->head.load(std::memory_order_acquire);

            if (dummy != this->head.load(std::memory_order_acquire))
                continue;

            node_t* next = dummy->next.load(std::memory_order_acquire);

            if (next == nullptr) {
                return nullptr;
            }

            // if tail is behind, try to update it
            node_t* tail_now = tail.load(std::memory_order_acquire);
            if (tail_now == dummy) {        
                tail.compare_exchange_strong(     
                    tail_now, next,
                    std::memory_order_release,
                    std::memory_order_relaxed);
                continue;
            }                          

            if (head.compare_exchange_strong(dummy, next,
                                            std::memory_order_release,
                                            std::memory_order_relaxed)) {
                return new(dummy->storage) T(std::forward<args_t>(args)...);
            }
        }
    }
    template<typename T, size_t N>
    void object_pool<T, N>::release(T* obj) {
        obj->~T(); 

        node_t* new_node = new(obj) node_t();

        while (true) {
            auto old_tail = this->tail.load(std::memory_order_acquire);
            
            if (old_tail != this->tail.load(std::memory_order_acquire)) {
                continue; // Tail was updated, retry
            }
            
            auto next = old_tail->next.load(std::memory_order_acquire);
            if (next == nullptr) {
                if (old_tail->next.compare_exchange_strong(
                    next, new_node,
                    std::memory_order_release,
                    std::memory_order_relaxed
                )) {
                    this->tail.compare_exchange_strong(
                        old_tail, new_node,
                        std::memory_order_relaxed,
                        std::memory_order_relaxed
                    );                 
                    return;
                }
            } else {
                // Tail was not the last node, so we need to update it
                this->tail.compare_exchange_strong(
                    old_tail, next,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed
                );
            }
        }


    }

}