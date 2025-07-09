#pragma once

#include <climits>
#include <cstddef>
#include <mutex>
#include <semaphore>
#include <thread>
#include <condition_variable>
#include <list>
#include <coroutine>
#include <vector>
#include "structs/ms_queue_chunk.h"
namespace seele::coro::thread {
    

    class thread_pool_impl{

    public:
        static thread_pool_impl& get_instance();

        auto submit(std::coroutine_handle<> h){
            tasks.emplace_back(h);
            sem.release();
        }

        thread_pool_impl(const thread_pool_impl&) = delete;        
        thread_pool_impl(thread_pool_impl&&) = delete;
        thread_pool_impl& operator=(const thread_pool_impl&) = delete;
        thread_pool_impl& operator=(thread_pool_impl&&) = delete;
    private:        
    void worker(std::stop_token st);

        thread_pool_impl(size_t worker_count);        
        ~thread_pool_impl();  

        structs::ms_queue_chunk<std::coroutine_handle<>> tasks;        
        std::vector<std::jthread> workers;
        std::counting_semaphore<> sem;
    };

    inline auto dispatch(std::coroutine_handle<> handle) {
        thread_pool_impl::get_instance().submit(handle);
    }    

    struct dispatch_awaiter{
        bool await_ready() { return false; }

        void await_suspend(std::coroutine_handle<> handle) {
            dispatch(handle);
        }

        void await_resume() {}

        explicit dispatch_awaiter(){}
    };

}






