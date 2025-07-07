#include "coro/threadpool.h"

namespace seele::coro::thread {

    thread_pool_impl& thread_pool_impl::get_instance(){
        static thread_pool_impl instance{4};
        return instance;
    } 


    void thread_pool_impl::worker(std::stop_token st){
        while(sem.acquire(), !st.stop_requested()){
            
           auto h = tasks.pop_front();

            h->resume();
        }
    }
    thread_pool_impl::thread_pool_impl(size_t worker_count) : sem{0} {
        workers.reserve(worker_count);
        for(size_t i = 0; i < worker_count; ++i){
            workers.emplace_back([this](std::stop_token st){
                this->worker(st);
            });
        }
    }

    thread_pool_impl::~thread_pool_impl() {
        for (auto& worker : workers) {
            worker.request_stop();
        }
        sem.release(workers.size());
    }



}