#pragma once
#include <atomic>
#include <cstddef>
#include <memory>
#include <new>
#include <optional>
#include <algorithm>
#include <utility>
#include "hp.h" 
namespace seele::structs {

template<typename T, size_t MAX_NODES = 64>
struct msc_chunk{            
    enum status_t {
        EMPTY,
        READY,
        USED
    };
    struct node_t{
        alignas(T) std::byte storage[sizeof(T)];
        std::atomic<status_t> status;
        T& get() {
            return *std::launder(reinterpret_cast<T*>(&storage));
        }
        node_t() : storage{}, status(EMPTY) {}
    };

    node_t data[MAX_NODES];
    std::atomic<size_t> read_index;
    std::atomic<size_t> write_index;
    std::atomic<msc_chunk*> next;
    msc_chunk() : data{}, read_index(0), write_index(0), next(nullptr) {}

    std::optional<T> pop_front();

    template<typename... args_t>
    bool emplace_back(args_t&&... args);

    ~msc_chunk();
};

template<typename T, size_t MAX_NODES>
std::optional<T> msc_chunk<T, MAX_NODES>::pop_front(){
    while (true) {
        size_t read_idx = read_index.load(std::memory_order_acquire);
        size_t write_idx = write_index.load(std::memory_order_acquire);
        if (read_idx == write_idx) {
            return std::nullopt; // no elements to pop
        }

        if (this->read_index.compare_exchange_strong(read_idx, read_idx + 1, std::memory_order_release, std::memory_order_relaxed)) {
            while (data[read_idx].status.load(std::memory_order_acquire) != READY) {
                // wait until the data is ready
            }

            T result = std::move(data[read_idx].get());
            data[read_idx].get().~T();
            data[read_idx].status.store(USED, std::memory_order_release);
            return result;
        }                
    }

}

template<typename T, size_t MAX_NODES>
template<typename... args_t>
bool msc_chunk<T, MAX_NODES>::emplace_back(args_t&&... args) {

    while (true) {
        size_t write_idx = write_index.load(std::memory_order_acquire);
        if (write_idx == MAX_NODES) {
            return false; // chunk is full
        }
        if (this->write_index.compare_exchange_strong(write_idx, write_idx + 1, std::memory_order_release, std::memory_order_relaxed)){
            // construct new value in place
            new (&data[write_idx].storage) T(std::forward<args_t>(args)...);
            data[write_idx].status.store(READY, std::memory_order_release);
            return true; // successfully added
        }
    }
}
template<typename T, size_t MAX_NODES>
msc_chunk<T, MAX_NODES>::~msc_chunk(){
    for (size_t i = read_index; i < write_index; ++i) {
        data[i].get().~T(); // call destructor if the data was constructed
    }
};


template <typename T, size_t N = 64>
class msc_queue {
private:
    using chunk_t = msc_chunk<T, N>;

public:
    msc_queue();
    ~msc_queue();

    void push_back(const T& item) { emplace_back(item); }

    void push_back(T&& item) { emplace_back(std::move(item)); }

    template<typename... args_t>
    void emplace_back(args_t&&... args);
    
    std::optional<T> pop_front();

private:
    alignas(64) std::atomic<chunk_t*> head_chunk;
    alignas(64) std::atomic<chunk_t*> tail_chunk;
    alignas(64) hazard_manager hp;
};


template <typename T, size_t N>
msc_queue<T, N>::msc_queue(){
    chunk_t* dummy = new chunk_t();
    head_chunk.store(dummy, std::memory_order_relaxed);
    tail_chunk.store(dummy, std::memory_order_relaxed);
}

template <typename T, size_t N>
msc_queue<T, N>::~msc_queue() {
    chunk_t* current = head_chunk.load(std::memory_order_relaxed);
    while (current) {
        chunk_t* next = current->next.load(std::memory_order_relaxed);
        delete current;
        current = next;
    }
}

template <typename T, size_t N>
template<typename... args_t>
void msc_queue<T, N>::emplace_back(args_t&&... args) {

    constexpr std::size_t HAZ_TAIL = 0;


    
    while (true) {
        chunk_t* old_tail = this->tail_chunk.load(std::memory_order_acquire);
        hp.protect<HAZ_TAIL>(old_tail);
        if (old_tail != this->tail_chunk.load(std::memory_order_acquire)) {
            continue; // Tail was updated, retry
        }
        if (old_tail->emplace_back(std::forward<args_t>(args)...)) {
            hp.clear<HAZ_TAIL>();
            return; // successfully added
        }

        // If we reach here, it means the current chunk is full
        // We need to create a new chunk and link it

        static thread_local std::unique_ptr<chunk_t> local_cache = std::make_unique<chunk_t>();

        chunk_t* new_chunk = local_cache.get();

        auto next = old_tail->next.load(std::memory_order_acquire);

        if (next == nullptr) {
            if (old_tail->next.compare_exchange_strong(
                next, new_chunk,
                std::memory_order_release,
                std::memory_order_relaxed
            )) {
                this->tail_chunk.compare_exchange_strong(
                    old_tail, new_chunk,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed
                );
                hp.clear<HAZ_TAIL>(); 
                // successfully linked new chunk
                // release the local cache
                // and reset it
                local_cache.release();
                local_cache = std::make_unique<chunk_t>();       
                continue;
            }

        }
        // Tail was not the last node, so we need to update it
        this->tail_chunk.compare_exchange_strong(
            old_tail, next,
            std::memory_order_relaxed,
            std::memory_order_relaxed
        );
        hp.clear<HAZ_TAIL>(); 
                        
    }
}

template <typename T, size_t N>
std::optional<T> msc_queue<T, N>::pop_front() {
    constexpr std::size_t HAZ_HEAD = 0;
    constexpr std::size_t HAZ_NEXT = 1;

    while (true) {
        chunk_t* dummy = this->head_chunk.load(std::memory_order_acquire);
        hp.protect<HAZ_HEAD>(dummy);

        if (dummy != this->head_chunk.load(std::memory_order_acquire))
            continue;

        auto res = dummy->pop_front();
        if (res) {
            hp.clear<HAZ_HEAD>();
            return res; // successfully popped
        }

        // If we reach here, it means the current chunk is empty
        // We need to update the head and possibly the tail

        chunk_t* next = dummy->next.load(std::memory_order_acquire);
        hp.protect<HAZ_NEXT>(next);

        if (next == nullptr) {
            hp.clear_all();
            return std::nullopt;
        }




        // if tail is behind, try to update it
        chunk_t* tail_now = tail_chunk.load(std::memory_order_acquire);
        if (tail_now == dummy) {        
            tail_chunk.compare_exchange_strong(     
                tail_now, next,
                std::memory_order_release,
                std::memory_order_relaxed);
            continue;
        }                          

        if (head_chunk.compare_exchange_strong(dummy, next,
                                        std::memory_order_release,
                                        std::memory_order_relaxed)) {
            hp.clear<HAZ_HEAD>();
            hp.clear<HAZ_NEXT>();
            hp.retire(dummy);
            // successfully updated head
        }
    }
}



}