#pragma once
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <print>
#include <vector>

namespace seele::structs {


    // only for single producer single consumer use case
    template <typename T>
    class spsc_object_pool {
    public:        
        struct alignas(alignof(T)) storage_t{
            std::byte data[sizeof(T)];
        };
        spsc_object_pool(size_t length) : storage(new storage_t[length + 1]), head(0), tail(0) {
            free_list.reserve(length + 1);
            for (size_t i = 0; i < length + 1; ++i) {
                free_list.push_back(storage[i].data);
            }
        }
        ~spsc_object_pool() {

            if (auto it = std::ranges::find(free_list, nullptr); it != free_list.end()) {
                std::println("Memory leak detected in special_object_pool, object at index {} was not deallocated", it - free_list.begin());
            }

            delete[] storage;
        }

        template<typename... args_t>
        T* allocate(args_t&&... args);

        void deallocate(T* obj);

    private:
        storage_t* storage;
        std::vector<void*> free_list;
        alignas(64) std::atomic<size_t> head;
        alignas(64) std::atomic<size_t> tail;
    };

    template <typename T>
    template <typename... args_t>
    T* spsc_object_pool<T>::allocate(args_t&&... args) {
        size_t idx = tail.load(std::memory_order_acquire);
        size_t next_idx = (idx + 1) % free_list.size();
        
        if (next_idx == head.load(std::memory_order_acquire)) {
            return nullptr; // No free object available
        }
        T* obj = new(free_list[idx]) T(std::forward<args_t>(args)...);
        free_list[idx] = nullptr; // Mark as used
        tail.store(next_idx, std::memory_order_release);
        return obj;
    }

    template <typename T>
    void spsc_object_pool<T>::deallocate(T* obj) {
        obj->~T();
        free_list[head] = static_cast<void*>(obj); // Add back to free list
        head.store((head.load(std::memory_order_acquire) + 1) % free_list.size(), std::memory_order_release);
    }

}