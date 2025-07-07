#pragma once
#include "structs/ms_queue_chunk.h"
#include <atomic>
#include <coroutine>
#include <cstddef>
#include <mutex>
#include <bit>
#include <liburing.h>
#include <cstdint>
#include <errno.h>
#include <print>
#include <semaphore>
#include <thread>
#include <type_traits>
#include <stop_token>
#include <cstring>

constexpr size_t submit_threshold = 64;

class io_ctx{
public:      
    struct request{        
        std::coroutine_handle<void> handle;        
        io_uring_cqe* cqe;
        bool is_timeout_link;
        __kernel_timespec* time_out;
        void* helper_ptr;
        auto (*sqe_handle)(void*, io_uring_sqe*) -> void;
    };  
    io_ctx(const io_ctx&) = delete;
    io_ctx(io_ctx&&) = delete;
    io_ctx& operator=(const io_ctx&) = delete;
    io_ctx& operator=(io_ctx&&) = delete;

    io_ctx(uint32_t entries = 1024, uint32_t flags = 0) : unp_sem{0}, max_entries{entries}, pending_sqe_count{0}{
        this->worker_thread = std::jthread([&] (std::stop_token st) { worker(st); });
        io_uring_queue_init(entries, &ring, flags);
    }
    ~io_ctx();


    void submit(request* req){
        this->unprocessed_requests.emplace_back(req);
        this->unp_sem.release();
    }

    void worker(std::stop_token st);

    void listener(std::stop_token st);

    inline void run(){ listener(stop_src.get_token()); }
    inline void request_stop() { stop_src.request_stop(); }

    inline static io_ctx& get_instance() {
        static io_ctx instance;
        return instance;
    }
private:    
    io_uring ring;
    std::stop_source stop_src;
    

    std::counting_semaphore<> unp_sem;
    seele::structs::ms_queue_chunk<request*> unprocessed_requests;
    std::jthread worker_thread;

    const size_t max_entries;
    std::atomic<size_t> pending_sqe_count;
};