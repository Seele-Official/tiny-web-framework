#pragma once
#include <atomic>
#include <coroutine>
#include <cstddef>
#include <mutex>
#include <bit>
#include <liburing.h>
#include <cstdint>
#include <errno.h>
#include <print>
#include <thread>
#include <type_traits>
#include <stop_token>
#include <cstring>

constexpr size_t submit_threshold = 64;

class io_ctx{
public:    
    io_ctx(const io_ctx&) = delete;
    io_ctx(io_ctx&&) = delete;
    io_ctx& operator=(const io_ctx&) = delete;
    io_ctx& operator=(io_ctx&&) = delete;

    io_ctx(uint32_t entries = 1024, uint32_t flags = 0) : max_entries{entries}, pending_request_count{0}, unprocessed_sqe_count{0} {
        io_uring_queue_init(entries, &ring, flags);
    }
    ~io_ctx();



    template<typename sqe_handle_t>
        requires std::is_invocable_v<sqe_handle_t, io_uring_sqe*>
    bool submit(std::coroutine_handle<void> handle, io_uring_cqe* cqe, sqe_handle_t&& sqe_handle);


    template<typename sqe_handle_t>
        requires std::is_invocable_v<sqe_handle_t, io_uring_sqe*>
    bool submit_and_link_timeout(std::coroutine_handle<void> handle, __kernel_timespec* time_out, io_uring_cqe* cqe, sqe_handle_t&& sqe_handle);



    void worker(std::stop_token stop_token);
    inline void run(){ worker(stop_src.get_token()); }
    inline void request_stop() { stop_src.request_stop(); }

    inline static io_ctx& get_instance() {
        static io_ctx instance;
        return instance;
    }
private:    
    struct request {
        io_uring_cqe* cqe;
        std::coroutine_handle<void> handle;
    };
    io_uring ring;
    std::mutex ring_m;
    std::stop_source stop_src;
    const size_t max_entries;
    std::atomic<size_t> pending_request_count;
    std::atomic<size_t> unprocessed_sqe_count;
};

template<typename sqe_handle_t>
    requires std::is_invocable_v<sqe_handle_t, io_uring_sqe*>
bool io_ctx::submit(std::coroutine_handle<void> handle, io_uring_cqe* cqe, sqe_handle_t&& sqe_handle) {
    {
        std::lock_guard<std::mutex> lock(ring_m);
        io_uring_sqe* sqe = io_uring_get_sqe(&ring);
        if (!sqe) {
            return false; 
        }
        sqe_handle(sqe);
        sqe->user_data = std::bit_cast<std::uintptr_t>(new request{ cqe, handle });
    }
    this->unprocessed_sqe_count.fetch_add(1, std::memory_order_release);
    if (this->unprocessed_sqe_count.load(std::memory_order_acquire) >= submit_threshold) {
        int sum_submit = 0;
        {
            std::lock_guard<std::mutex> lock(ring_m);
            sum_submit = io_uring_submit(&ring);
        }
        if (sum_submit >= 0){
            this->pending_request_count.fetch_add(sum_submit, std::memory_order_release);
            this->unprocessed_sqe_count.fetch_sub(sum_submit, std::memory_order_release);
        } else {
            std::print("io_uring_submit failed: {}\n", strerror(-sum_submit));
            return false; 
        }
    }
    return true;
}


template<typename sqe_handle_t>
    requires std::is_invocable_v<sqe_handle_t, io_uring_sqe*>
bool io_ctx::submit_and_link_timeout(std::coroutine_handle<void> handle, __kernel_timespec* time_out, io_uring_cqe* cqe, sqe_handle_t&& sqe_handle){
    {
        std::lock_guard<std::mutex> lock(ring_m);
        io_uring_sqe* sqe = io_uring_get_sqe(&ring);
        if (!sqe) {
            return false; 
        }      
        sqe_handle(sqe);
        sqe->user_data = std::bit_cast<std::uintptr_t>(new request{ cqe, handle });
        sqe->flags |= IOSQE_IO_LINK;

        auto* timeout_sqe = io_uring_get_sqe(&ring);
        if (!timeout_sqe) {
            delete std::bit_cast<request*>(sqe->user_data);
            return false; 
        }
        io_uring_prep_link_timeout(timeout_sqe, time_out, 0);
        timeout_sqe->user_data = ETIMEDOUT;      
    }


    this->unprocessed_sqe_count.fetch_add(2, std::memory_order_release);

    if (this->unprocessed_sqe_count.load(std::memory_order_acquire) >= submit_threshold) {
        int sum_submit = 0;
        {
            std::lock_guard<std::mutex> lock(ring_m);
            sum_submit = io_uring_submit(&ring);
        }
        if (sum_submit >= 0){
            this->pending_request_count.fetch_add(sum_submit, std::memory_order_release);
            this->unprocessed_sqe_count.fetch_sub(sum_submit, std::memory_order_release);
        } else {
            std::print("io_uring_submit failed: {}\n", strerror(-sum_submit));
            return false; 
        }
    }
    return true;
}
