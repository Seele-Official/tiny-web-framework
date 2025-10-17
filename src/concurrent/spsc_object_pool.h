#pragma once
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <print>
#include <vector>

namespace seele::concurrent {

// only for single producer single consumer use case
template <typename T>
class spsc_object_pool {
public:        
    struct alignas(alignof(T)) storage_t{
        std::byte data[sizeof(T)];
    };

    spsc_object_pool(size_t size) : 
        storage(new storage_t[size]), 
        free_ringbuffer(new std::atomic<void*>[size]), 
        pool_size(size), 
        head(size), 
        tail(0) 
    {
        for (size_t i = 0; i < size; ++i) {
            free_ringbuffer[i].store(storage[i].data, std::memory_order_release);
        }
    }
    ~spsc_object_pool() {

        for (size_t i = tail.load(std::memory_order_acquire); i < head.load(std::memory_order_acquire); ++i) {
            if (free_ringbuffer[i % pool_size].load(std::memory_order_acquire) == nullptr) {
                std::println("Memory leak detected in special_object_pool, object at index {} was not deallocated", i % pool_size);
            }
        }

        delete[] storage;
        delete[] free_ringbuffer;
    }

    template<typename... args_t>
    T* allocate(args_t&&... args);

    void deallocate(T* obj);

private:
    storage_t* storage;
    std::atomic<void*>* free_ringbuffer;
    size_t pool_size;
    alignas(64) std::atomic<size_t> head;
    alignas(64) std::atomic<size_t> tail;
};

template <typename T>
template <typename... args_t>
T* spsc_object_pool<T>::allocate(args_t&&... args) {
    size_t idx = tail.load(std::memory_order_acquire);
    if (idx >= head.load(std::memory_order_acquire)) {
        return nullptr; // No free object available
    }
    T* obj = new(free_ringbuffer[idx % pool_size].load(std::memory_order_acquire)) T(std::forward<args_t>(args)...);

    free_ringbuffer[idx % pool_size].store(nullptr, std::memory_order_release);

    tail.fetch_add(1, std::memory_order_release);
    return obj;
}

template <typename T>
void spsc_object_pool<T>::deallocate(T* obj) {
    obj->~T();
    size_t head = this->head.load(std::memory_order_acquire);
    free_ringbuffer[head % pool_size].store(static_cast<void*>(obj), std::memory_order_release); // Add back to free list
    this->head.fetch_add(1, std::memory_order_release);
}

}