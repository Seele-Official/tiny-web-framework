#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <optional>
#include <print>
#include <thread>
#include <variant>

#include "coro/thread.h"
#include "io/ctx.h"
#include "log.h"

namespace io::detail {
    
using std::chrono::operator""ms;
using std::chrono::operator""ns;


void ctx::worker(std::stop_token st){

    size_t submit_count = 0;
    size_t pending_req_count = 0;
    while (!st.stop_requested()) {

        if (unp_sem.try_acquire_for(25ms)) {
            auto req = unprocessed_requests.pop_front();
            auto& [helper_ptr, ring_handle] = req.value();
            submit_count += ring_handle(helper_ptr, &ring);

            pending_req_count++;
            
            if (submit_count >= submit_threshold) {
                auto submit_ret = io_uring_submit(&ring);
                if (submit_ret < 0) {
                    log::async::error("io_uring_submit failed: {}", strerror(-submit_ret));
                } else {
                    log::async::debug("Submitted {} requests to io_uring", submit_ret);
                    submit_count = 0;
                }
            }
        } else {
            if (submit_count) {
                this->pending_req_count.fetch_add(pending_req_count, std::memory_order_acq_rel);
                auto submit_ret = io_uring_submit(&ring);
                if (submit_ret < 0) {
                    log::async::error("io_uring_submit failed: {}", strerror(-submit_ret));
                } else {
                    log::async::debug("Submitted {} requests to io_uring", submit_ret);
                    submit_count = 0;
                    pending_req_count = 0;
                }
            }
        }
    }
    this->pending_req_count.fetch_add(pending_req_count, std::memory_order_acq_rel);
    this->is_worker_running.store(false, std::memory_order_release);
}


void ctx::handle_cqes(io_uring_cqe* cqe) {
    uint32_t head, count = 0;
    size_t processed_req = 0;
    io_uring_for_each_cqe(&ring, head, cqe) {
        count++;
        auto* data = std::bit_cast<usr_data*>(cqe->user_data);
        std::visit(
            [&]<typename T>(T& usr_data) {
                if constexpr (std::is_same_v<T, io_usr_data>) {
                    usr_data.io_ret->store(cqe->res, std::memory_order_release); // Copy the cqe result to the user data
                    coro::thread::dispatch(usr_data.handle);
                    this->pending_req_count.fetch_sub(1, std::memory_order_release);
                } else if constexpr (std::is_same_v<T, timeout_usr_data>) {
                    switch (cqe->res) {
                        case -ETIME:
                        case -ECANCELED:
                        case -ENOENT:
                            break; // Timeout or canceled or no entry, skip this cqe
                        default:{
                            auto* io_data = std::get_if<io_usr_data>(usr_data.io_data);
                            std::println("Timeout req is broken, handle {}, {}", *((void **)&io_data->handle), cqe->res);
                            std::terminate();
                        }
                    }
                } else {
                    log::async::error("Unknown user data type in cqe");
                }
            },
            *data
        );
        this->usr_data_pool.deallocate(data); // Clean up the user data
    }
    io_uring_cq_advance(&ring, count);
    this->pending_req_count.fetch_sub(processed_req, std::memory_order_acq_rel);
    log::async::debug("Processed {} completed requests", count);      
}



void ctx::start_listen(std::stop_token st){
    sigset_t sigmask;
    sigemptyset(&sigmask); 
    sigdelset(&sigmask, SIGINT);

    while (!st.stop_requested()) {
        io_uring_cqe* cqe;
        int ret = io_uring_wait_cqes(&ring, &cqe, 1, nullptr, &sigmask); 

        if (ret == -EINTR){
            log::async::debug("io_uring_wait_cqes interrupted by signal, checking for stop request");
            continue; // Interrupted by signal, continue waiting
        } else if (ret < 0) {
            log::sync::error("io_uring_wait_cqes failed: {}", strerror(-ret));
            break;
        } else {
            this->handle_cqes(cqe);
        }
    }
}

void ctx::clean_up() {
    while (this->is_worker_running.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(100ms);
    }

    // Clean up remaining requests

    size_t pending_req_count = 0;
    while (unp_sem.try_acquire_for(5ms)) {
        auto req = unprocessed_requests.pop_front();
        auto& [helper_ptr, ring_handle] = req.value();
        ring_handle(helper_ptr, &ring);
        pending_req_count++;
    }
    this->pending_req_count.fetch_add(pending_req_count, std::memory_order_acq_rel);
    auto submit_ret = io_uring_submit(&ring);
    if (submit_ret < 0) {
        log::async::error("io_uring_submit failed: {}", strerror(-submit_ret));
    } else {
        log::async::debug("Submitted {} requests to io_uring", submit_ret);
    }


    // wait for all requests to complete
    __kernel_timespec ts{ .tv_sec = 1, .tv_nsec = 0 };


    while (this->pending_req_count.load(std::memory_order_relaxed) > 0) {
        std::println("Waiting for {} pending requests to complete...", this->pending_req_count.load(std::memory_order_relaxed));
        io_uring_cqe* cqe;
        int ret = io_uring_wait_cqes(&ring, &cqe, 1, &ts, nullptr); 

        if (ret == -ETIME){
            continue; // Timeout, continue waiting
        } else if (ret < 0) {
            log::sync::error("io_uring_wait_cqes failed: {}", strerror(-ret));
            break;
        } else {
            this->handle_cqes(cqe);
        }
    }
}


ctx::~ctx() {
    io_uring_queue_exit(&ring);
}

}