#pragma once

#include <atomic>
#include <coroutine>
#include <cstddef>
#include <liburing.h>
#include <cstdint>
#include <print>
#include <semaphore>
#include <string_view>
#include <thread>
#include <type_traits>
#include <stop_token>
#include <cstring>
#include "structs/mpsc_queue.h"
#include "structs/spsc_object_pool.h"
constexpr size_t submit_threshold = 64;

namespace coro_io {


    
class ctx{
public:      
    struct request{        
        void* helper_ptr;
        auto (*ring_handle)(void*, io_uring*) -> int;
    };
    struct io_usr_data;
    struct timeout_usr_data;
    using usr_data = std::variant<io_usr_data, timeout_usr_data>;
    struct io_usr_data{
        std::coroutine_handle<> handle;
        int32_t* io_ret;
    };


    struct timeout_usr_data{
        usr_data* io_data;
    };


    ctx(const ctx&) = delete;
    ctx(ctx&&) = delete;
    ctx& operator=(const ctx&) = delete;
    ctx& operator=(ctx&&) = delete;

    usr_data* new_usr_data(usr_data data){
        usr_data* data_ptr;
        do {
            data_ptr = this->usr_data_pool.allocate(data);
        } while (data_ptr == nullptr);

        return data_ptr;
    }

    inline int32_t register_file_alloc_range(uint32_t off, uint32_t len) {
        return io_uring_register_file_alloc_range(&ring, off, len);
    }

    inline int32_t register_files_sparse(uint32_t count) {
        return io_uring_register_files_sparse(&ring, count);
    }

    inline int32_t register_files(const int32_t* fds, uint32_t count) {
        return io_uring_register_files(&ring, fds, count);
    }
    inline int32_t unregister_files() {
        return io_uring_unregister_files(&ring);
    }

    inline bool submit(void* helper_ptr, auto (*ring_handle)(void*, io_uring*) -> int) {
        if (this->is_worker_running.load(std::memory_order_acquire)){
            this->unprocessed_requests.emplace_back(helper_ptr, ring_handle);
            this->unp_sem.release();            
            return true;
        }
        return false;
    }


    inline void request_stop() { stop_src.request_stop(); }
    
    inline void run(){ 
        this->start_listen(stop_src.get_token());
        this->clean_up(); 
    }

    void clean_up();

    inline static ctx& get_instance() {
        static ctx instance;
        return instance;
    }
private: 

    void worker(std::stop_token st);

    void start_listen(std::stop_token st);

    void handle_cqes(io_uring_cqe* cqe);

    ctx(uint32_t entries = 128, uint32_t flags = 0) : max_entries{entries}, pending_req_count{0}, unp_sem{0}, usr_data_pool{1024*128} {
        if(io_uring_queue_init(entries, &ring, flags) < 0) {
            std::println("Failed to initialize io_uring");
            std::terminate();
        }
        this->worker_thread = std::jthread([&] (std::stop_token st) { worker(st); }, stop_src.get_token());
        this->is_worker_running.store(true, std::memory_order_release);
    }
    ~ctx();  



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
}
