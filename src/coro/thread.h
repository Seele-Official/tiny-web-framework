#pragma once
#include <cstddef>
#include <semaphore>
#include <thread>
#include <coroutine>
#include <vector>
#include "concurrent/mpmc_queue.h"
namespace coro::thread {
    
namespace detail {
class pool {
public:
    static pool& get_instance();

    void submit(std::coroutine_handle<> h){
        tasks.emplace_back(h);
        sem.release();
    }

    bool init(size_t worker_count);

    pool(const pool&) = delete;        
    pool(pool&&) = delete;
    pool& operator=(const pool&) = delete;
    pool& operator=(pool&&) = delete;
private:        

    void worker(std::stop_token st);

    pool() = default;        
    ~pool();  

    concurrent::mpmc_queue<std::coroutine_handle<>> tasks{};        
    std::vector<std::jthread> workers{};
    std::counting_semaphore<> sem{0};
};

}

inline void dispatch(std::coroutine_handle<> handle) {
    detail::pool::get_instance().submit(handle);
}

inline bool init(size_t worker_count) {
    return detail::pool::get_instance().init(worker_count);
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






