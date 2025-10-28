#include "coro/thread.h"
#include <exception>
#include <print>

namespace coro::thread::detail {

pool& pool::get_instance(){
    static pool instance{};
    return instance;
} 


void pool::worker(std::stop_token st){
    while(sem.acquire(), !st.stop_requested()){
        
        auto h = tasks.pop_front();

        h->resume();
    }
}
bool pool::init(size_t worker_count) {
    static bool flag = false;
    if (flag) {
        return false;
    }
    workers.reserve(worker_count);
    for(size_t i = 0; i < worker_count; ++i){
        workers.emplace_back([this](std::stop_token st){
            this->worker(st);
        });
    }
    flag = true;
    return true;
}

pool::~pool() {
    for (auto& worker : workers) {
        worker.request_stop();
    }
    sem.release(workers.size());
}



}