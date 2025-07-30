#pragma once
#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>
#include <thread>
#include <ranges>
#include <algorithm>
#include <utility>
#include "mpsc_hp.h"
namespace seele::structs {


    template<typename T, size_t MAX_NODES = 64>
    struct mpsc_chunk{
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
        size_t read_index;
        std::atomic<size_t> write_index;
        std::atomic<mpsc_chunk*> next;
        mpsc_chunk() : data{}, read_index(0), write_index(0), next(nullptr) {}

        std::optional<T> pop_front();

        template<typename... args_t>
        bool emplace_back(args_t&&... args);

        ~mpsc_chunk();
    };

    template<typename T, size_t MAX_NODES>
    std::optional<T> mpsc_chunk<T, MAX_NODES>::pop_front(){
        size_t write_idx = write_index.load(std::memory_order_acquire);
        if (this->read_index >= write_idx) {
            return std::nullopt; // no elements to pop
        }

        
        while (data[this->read_index].status.load(std::memory_order_acquire) != READY) {
            // wait until the data is ready
        }

        T result = std::move(data[this->read_index].get());
        data[this->read_index].get().~T();
        data[this->read_index].status.store(USED, std::memory_order_release);
        this->read_index++;
        return result;
    }

    template<typename T, size_t MAX_NODES>
    template<typename... args_t>
    bool mpsc_chunk<T, MAX_NODES>::emplace_back(args_t&&... args) {

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
    mpsc_chunk<T, MAX_NODES>::~mpsc_chunk(){
        for (size_t i = read_index; i < write_index; ++i) {
            data[i].get().~T(); // call destructor if the data was constructed
        }
    };


    template <typename T, size_t N = 64>
    class mpsc_queue {
    private:
        using chunk_t = mpsc_chunk<T, N>;

    public:
        mpsc_queue();
        ~mpsc_queue();

        void push_back(const T& item) { emplace_back(item); }

        void push_back(T&& item) { emplace_back(std::move(item)); }

        template<typename... args_t>
        void emplace_back(args_t&&... args);
        
        std::optional<T> pop_front();
        

    private:
        alignas(64) chunk_t* head_chunk;
        alignas(64) std::atomic<chunk_t*> tail_chunk;
        alignas(64) mpsc_hazard_manager hp;
    };


    template <typename T, size_t N>
    mpsc_queue<T, N>::mpsc_queue(){
        chunk_t* dummy = new chunk_t();
        head_chunk = dummy;
        tail_chunk.store(dummy, std::memory_order_relaxed);
    }

    template <typename T, size_t N>
    mpsc_queue<T, N>::~mpsc_queue() {
        chunk_t* current = head_chunk;
        while (current) {
            chunk_t* next = current->next.load(std::memory_order_relaxed);
            delete current;
            current = next;
        }
    }

    template <typename T, size_t N>
    template<typename... args_t>
    void mpsc_queue<T, N>::emplace_back(args_t&&... args) {

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
            
            chunk_t* new_chunk = new chunk_t();                
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
                    continue; // successfully linked new chunk
                }

            }
            // Tail was not the last node, so we need to update it
            this->tail_chunk.compare_exchange_strong(
                old_tail, next,
                std::memory_order_relaxed,
                std::memory_order_relaxed
            );
            hp.clear<HAZ_TAIL>(); 
                         
            
            delete new_chunk; // clean up the new chunk if it was not used
        }
    }


    template <typename T, size_t N>
    std::optional<T> mpsc_queue<T, N>::pop_front() {
        
        while (true) {
            chunk_t* dummy = this->head_chunk;
            auto res = dummy->pop_front();
            if (res) {
                return res; // successfully popped
            }

            // If we reach here, it means the current chunk is empty
            // We need to update the head and possibly the tail

            chunk_t* next = dummy->next.load(std::memory_order_acquire);
            if (next == nullptr) {
                return std::nullopt;
            }


            // if tail is behind, try to update it
            chunk_t* tail_now = tail_chunk.load(std::memory_order_acquire);
            if (tail_now == dummy) {        
                tail_chunk.compare_exchange_strong(     
                    tail_now, next,
                    std::memory_order_release,
                    std::memory_order_relaxed);
            }                          

            head_chunk = next;
            hp.retire(dummy); // retire the old head chunk
            // successfully updated head        
        }

    }



}