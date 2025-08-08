#pragma once
#include <atomic>
#include <climits>
#include <cstddef>
#include <semaphore>
#include <thread>
#include <coroutine>
#include <vector>
#include "structs/msc_queue.h"
namespace seele::coro::thread {
    

class pool{
public:
    static pool& get_instance();

    auto submit(std::coroutine_handle<> h){
        tasks.emplace_back(h);
        sem.release();
    }

    pool(const pool&) = delete;        
    pool(pool&&) = delete;
    pool& operator=(const pool&) = delete;
    pool& operator=(pool&&) = delete;
private:        

    void worker(std::stop_token st);

    pool(size_t worker_count);        
    ~pool();  

    structs::msc_queue<std::coroutine_handle<>> tasks;        
    std::vector<std::jthread> workers;
    std::counting_semaphore<> sem;
};

inline auto dispatch(std::coroutine_handle<> handle) {
    pool::get_instance().submit(handle);
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






