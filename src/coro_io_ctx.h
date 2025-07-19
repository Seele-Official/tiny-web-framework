#pragma once

#include <atomic>
#include <coroutine>
#include <cstddef>
#include <liburing.h>
#include <cstdint>
#include <semaphore>
#include <thread>
#include <type_traits>
#include <stop_token>
#include <cstring>
#include "structs/mpsc_queue.h"
#include "structs/spsc_object_pool.h"
constexpr size_t submit_threshold = 64;

class coro_io_ctx{
public:      
    struct request{        
        std::coroutine_handle<void> handle;        
        io_uring_cqe* cqe;
        bool is_timeout_link;
        __kernel_timespec* time_out;
        void* helper_ptr;
        auto (*sqe_handle)(void*, io_uring_sqe*) -> void;
    };
    struct io_usr_data;
    struct timeout_usr_data;
    using usr_data = std::variant<io_usr_data, timeout_usr_data>;
    struct io_usr_data{
        std::coroutine_handle<> handle;
        io_uring_cqe* cqe;
    };


    struct timeout_usr_data{
        usr_data* io_data;
    };


    coro_io_ctx(const coro_io_ctx&) = delete;
    coro_io_ctx(coro_io_ctx&&) = delete;
    coro_io_ctx& operator=(const coro_io_ctx&) = delete;
    coro_io_ctx& operator=(coro_io_ctx&&) = delete;

    void submit(std::coroutine_handle<void> handle, io_uring_cqe* cqe, bool is_timeout_link, __kernel_timespec* time_out, void* helper_ptr, auto (*sqe_handle)(void*, io_uring_sqe*) -> void ) {
        this->unprocessed_requests.emplace_back(handle, cqe, is_timeout_link, time_out, helper_ptr, sqe_handle);
        this->unp_sem.release();
    }

    void worker(std::stop_token st);

    void start_listen(std::stop_token st);

    inline void run(){ start_listen(stop_src.get_token()); }
    inline void request_stop() { stop_src.request_stop(); }

    inline static coro_io_ctx& get_instance() {
        static coro_io_ctx instance;
        return instance;
    }
private:  
    coro_io_ctx(uint32_t entries = 1024, uint32_t flags = 0) : max_entries{entries}, pending_req_count{0}, unp_sem{0}, usr_data_pool{1024*128} {
        this->worker_thread = std::jthread([&] (std::stop_token st) { worker(st); }, stop_src.get_token());
        io_uring_queue_init(entries, &ring, flags);
    }
    ~coro_io_ctx();  



    io_uring ring;
    std::stop_source stop_src;
    std::jthread worker_thread; 
    std::atomic<bool> is_worker_running;
    const size_t max_entries;
    std::atomic<size_t> pending_req_count;

    std::counting_semaphore<> unp_sem;
    seele::structs::mpsc_queue<request> unprocessed_requests;

    seele::structs::spsc_object_pool<usr_data> usr_data_pool;

};