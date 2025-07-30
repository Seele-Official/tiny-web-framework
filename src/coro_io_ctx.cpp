#include "coro_io_ctx.h"
#include "coro/threadpool.h"
#include "log.h"
#include "math.h"
#include <array>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <coroutine>
#include <cstddef>
#include <cstring>
#include <liburing.h>
#include <optional>
#include <print>
#include <thread>
#include <variant>
#include <vector>
using namespace seele;
using std::chrono::operator""ms;
using std::chrono::operator""ns;


namespace coro_io {



void ctx::worker(std::stop_token st){

    size_t submit_count = 0;
    while (!st.stop_requested()) {

        if (unp_sem.try_acquire_for(25ms)) {
            auto req = unprocessed_requests.pop_front();
            auto& [helper_ptr, ring_handle] = req.value();
            submit_count += ring_handle(helper_ptr, &ring);
            this->pending_req_count.fetch_add(1, std::memory_order_release);

            if (submit_count >= submit_threshold) {
                auto submit_ret = io_uring_submit(&ring);
                if (submit_ret < 0) {
                    log::async().error("io_uring_submit failed: {}", strerror(-submit_ret));
                } else {
                    log::async().debug("Submitted {} requests to io_uring", submit_ret);
                    submit_count = 0;
                }
            }
        } else {
            if (submit_count) {
                auto submit_ret = io_uring_submit(&ring);
                if (submit_ret < 0) {
                    log::async().error("io_uring_submit failed: {}", strerror(-submit_ret));
                } else {
                    log::async().debug("Submitted {} requests to io_uring", submit_ret);
                    submit_count = 0;
                }
            }
        }
    }
    this->is_worker_running.store(false, std::memory_order_release);
}


void ctx::handle_cqes(io_uring_cqe* cqe) {
    int head, count = 0;
    
    io_uring_for_each_cqe(&ring, head, cqe) {
        count++;
        auto* data = std::bit_cast<usr_data*>(cqe->user_data);
        std::visit(
            [&]<typename T>(T& usr_data) {
                if constexpr (std::is_same_v<T, io_usr_data>) {
                    *usr_data.io_ret = cqe->res; // Copy the cqe result to the user data
                    coro::thread::dispatch(usr_data.handle);
                    this->pending_req_count.fetch_sub(1, std::memory_order_release);
                } else if constexpr (std::is_same_v<T, timeout_usr_data>) {
                    switch (cqe->res) {
                        case -ETIME:
                        case -ECANCELED:
                            break; // Timeout or canceled, skip this cqe
                        default:{
                            // The chain of timeout req is broken, because the io req in error state
                            // Io uring only return error of the io req, so we need transfer the error
                            auto* io_data = std::get_if<io_usr_data>(usr_data.io_data);
                            *io_data->io_ret = cqe->res;
                            std::println("Timeout req is broken, handle {}, {}", math::tohex(io_data->handle), cqe->res);
                            coro::thread::dispatch(io_data->handle);
                            this->usr_data_pool.deallocate(usr_data.io_data); // Clean up the io data
                            this->pending_req_count.fetch_sub(1, std::memory_order_release);
                        }
                    }
                } else {
                    log::async().error("Unknown user data type in cqe");
                }
            },
            *data
        );
        this->usr_data_pool.deallocate(data); // Clean up the user data
    }
    io_uring_cq_advance(&ring, count);
    log::async().debug("Processed {} completed requests", count);      
}



void ctx::start_listen(std::stop_token st){
    sigset_t sigmask;
    sigemptyset(&sigmask); 
    sigdelset(&sigmask, SIGINT);

    while (!st.stop_requested()) {
        io_uring_cqe* cqe;
        int ret = io_uring_wait_cqes(&ring, &cqe, 1, nullptr, &sigmask); 

        if (ret == -EINTR){
            log::async().debug("io_uring_wait_cqes interrupted by signal, checking for stop request");
            continue; // Interrupted by signal, continue waiting
        } else if (ret < 0) {
            log::sync().error("io_uring_wait_cqes failed: {}", strerror(-ret));
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
    while (unp_sem.try_acquire_for(5ms)) {
        auto req = unprocessed_requests.pop_front();
        auto& [helper_ptr, ring_handle] = req.value();
        ring_handle(helper_ptr, &ring);
        this->pending_req_count.fetch_add(1, std::memory_order_release);
    }
    auto submit_ret = io_uring_submit(&ring);
    if (submit_ret < 0) {
        log::async().error("io_uring_submit failed: {}", strerror(-submit_ret));
    } else {
        log::async().debug("Submitted {} requests to io_uring", submit_ret);
    }


    // wait for all requests to complete
    __kernel_timespec ts{ .tv_sec = 1, .tv_nsec = 0 };


    while (this->pending_req_count.load() > 0) {
        std::println("Waiting for {} pending requests to complete...", this->pending_req_count.load());
        io_uring_cqe* cqe;
        int ret = io_uring_wait_cqes(&ring, &cqe, 1, &ts, nullptr); 

        if (ret == -ETIME){
            continue; // Timeout, continue waiting
        } else if (ret < 0) {
            log::sync().error("io_uring_wait_cqes failed: {}", strerror(-ret));
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