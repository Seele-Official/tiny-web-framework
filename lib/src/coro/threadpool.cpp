#include "coro/threadpool.h"

namespace seele::coro::thread {

pool& pool::get_instance(){
    static pool instance{4};
    return instance;
} 


void pool::worker(std::stop_token st){
    while(sem.acquire(), !st.stop_requested()){
        
        auto h = tasks.pop_front();

        h->resume();
    }
}
pool::pool(size_t worker_count) : sem{0} {
    workers.reserve(worker_count);
    for(size_t i = 0; i < worker_count; ++i){
        workers.emplace_back([this](std::stop_token st){
            this->worker(st);
        });
    }
}

pool::~pool() {
    for (auto& worker : workers) {
        worker.request_stop();
    }
    sem.release(workers.size());
}



}