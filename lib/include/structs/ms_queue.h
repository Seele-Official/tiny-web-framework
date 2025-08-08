#pragma once
#include <atomic>
#include <memory>
#include <optional>
#include "hp.h"
namespace seele::structs {
template <typename T>
class ms_queue {
private:
    struct node_t{
        T data;
        std::atomic<node_t*> next;
        node_t() : next(nullptr) {}
        node_t(const T& item) : data(item), next(nullptr) {}

        template<typename... args_t>
        node_t(args_t&&... args) : data(std::forward<args_t>(args)...), next(nullptr) {}

        ~node_t() = default;
    };

public:
    ms_queue();
    ~ms_queue();

    void push_back(const T& item) { emplace_back(item); }

    void push_back(T&& item) { emplace_back(std::move(item)); }

    template<typename... args_t>
    void emplace_back(args_t&&... args);
    
    std::optional<T> pop_front();

private:
    alignas(64) std::atomic<node_t*> head;
    alignas(64) std::atomic<node_t*> tail;
    alignas(64) hazard_manager hp;
};


template <typename T>
ms_queue<T>::ms_queue(){
    node_t* dummy = new node_t();
    head.store(dummy, std::memory_order_relaxed);
    tail.store(dummy, std::memory_order_relaxed);
}

template <typename T>
ms_queue<T>::~ms_queue() {
    node_t* current = head.load(std::memory_order_relaxed);
    while (current) {
        node_t* next = current->next.load(std::memory_order_relaxed);
        delete current;
        current = next;
    }
}

template <typename T>
template<typename... args_t>
void ms_queue<T>::emplace_back(args_t&&... args) {
    constexpr std::size_t HAZ_TAIL = 0;
    node_t* new_node = new node_t(std::forward<args_t>(args)...);
    while (true) {
        auto old_tail = this->tail.load(std::memory_order_acquire);
        hp.protect<HAZ_TAIL>(old_tail);  
            
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
                hp.clear<HAZ_TAIL>();                     
                return;
            }
        } else {
            // Tail was not the last node, so we need to update it
            this->tail.compare_exchange_strong(
                old_tail, next,
                std::memory_order_relaxed,
                std::memory_order_relaxed
            );
            hp.clear<HAZ_TAIL>(); 
        }
    }
}

template <typename T>
std::optional<T> ms_queue<T>::pop_front() {
    constexpr std::size_t HAZ_HEAD = 0;
    constexpr std::size_t HAZ_NEXT = 1;

    while (true) {
        node_t* dummy = this->head.load(std::memory_order_acquire);
        hp.protect<HAZ_HEAD>(dummy);

        if (dummy != this->head.load(std::memory_order_acquire))
            continue;

        node_t* next = dummy->next.load(std::memory_order_acquire);
        hp.protect<HAZ_NEXT>(next);


        if (next == nullptr) {
            hp.clear_all();
            return std::nullopt;
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
            auto value = std::make_optional<T>(std::move(next->data));
            hp.clear_all();
            hp.retire(dummy);
            return value;
        }
    }
}

}