#include "coro_io_ctx.h"
#include "coro/threadpool.h"
#include "log.h"
#include <array>
#include <cerrno>
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




void coro_io_ctx::worker(std::stop_token st){



    this->is_worker_running.store(true, std::memory_order_release);
    size_t submit_count = 0;
    while (!st.stop_requested()) {

        if (unp_sem.try_acquire_for(5ms)) {
            auto req = unprocessed_requests.pop_front();
            auto& [handle, cqe, is_timeout_link, time_out, helper_ptr, sqe_handle] = req.value();
            if (is_timeout_link) {
                if (time_out) {
                    auto* sqe = io_uring_get_sqe(&ring);
                    auto* timeout_sqe = io_uring_get_sqe(&ring);
                    // Need to handle validation of sqe, but we assume the it's valid
                    sqe_handle(helper_ptr, sqe);
                    usr_data* io_data;
                    do {
                        io_data = this->usr_data_pool.allocate(
                            io_usr_data{handle, cqe}
                        );
                    } while (io_data == nullptr); 
                    sqe->user_data = std::bit_cast<std::uintptr_t>(io_data);                    
                    sqe->flags |= IOSQE_IO_LINK;

                    io_uring_prep_link_timeout(timeout_sqe, time_out, 0);

                    usr_data* timeout_data;
                    do {
                        timeout_data = this->usr_data_pool.allocate(
                            timeout_usr_data{io_data}
                        );
                    } while (timeout_data == nullptr);
                    timeout_sqe->user_data = std::bit_cast<std::uintptr_t>(timeout_data);

                    submit_count += 2;
                } else {
                    log::async().error("Timeout link requested but time_out is null");
                }
            } else {
                auto* sqe = io_uring_get_sqe(&ring);
                // Need to handle validation of sqe, but we assume the handle is valid
                sqe_handle(helper_ptr, sqe);

                usr_data* io_data;
                do {
                    io_data = this->usr_data_pool.allocate(
                        io_usr_data{handle, cqe}
                    );
                } while (io_data == nullptr);
                sqe->user_data = std::bit_cast<std::uintptr_t>(io_data);
                submit_count++;
                // Also need to handle validation of usr_data_pool allocation, but we assume it's valid
            }
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

    // Clean up remaining requests
    if (submit_count) {
        auto submit_ret = io_uring_submit(&ring);
        if (submit_ret < 0) {
            log::async().error("io_uring_submit failed: {}", strerror(-submit_ret));
        } else {
            log::async().debug("Submitted {} requests to io_uring", submit_ret);
            submit_count = 0;
        }
    }
    while (true) {
        auto ret = unprocessed_requests.pop_front();
        if (!ret.has_value()) {
            break; // No more requests to process
        }
        ret.value().handle.destroy();
    }
    this->is_worker_running.store(false, std::memory_order_release);
}




void coro_io_ctx::start_listen(std::stop_token st){
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
            int head;
            int count = 0;
            io_uring_for_each_cqe(&ring, head, cqe) {
                count++;
                auto* data = std::bit_cast<usr_data*>(cqe->user_data);
                std::visit(
                    [&]<typename T>(T& usr_data) {
                        if constexpr (std::is_same_v<T, io_usr_data>) {
                            *usr_data.cqe = *cqe; // Copy the cqe to the user data
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
                                    *io_data->cqe = *cqe;
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
    }

    while (this->is_worker_running.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(1000ms);
    }

}

coro_io_ctx::~coro_io_ctx() {
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
            int head;
            int count = 0;
            io_uring_for_each_cqe(&ring, head, cqe) {
                count++;
                auto* data = std::bit_cast<usr_data*>(cqe->user_data);
                std::visit(
                    [&]<typename T>(T& usr_data) {
                        if constexpr (std::is_same_v<T, io_usr_data>) {
                            *usr_data.cqe = *cqe; // Copy the cqe to the user data
                            usr_data.handle.destroy();
                            this->pending_req_count.fetch_sub(1, std::memory_order_release);
                        } else if constexpr (std::is_same_v<T, timeout_usr_data>) {
                            switch (cqe->res) {
                                case -ETIME:
                                case -ECANCELED:
                                    break; // Timeout or canceled, skip this cqe
                                default:{
                                    auto* io_data = std::get_if<io_usr_data>(usr_data.io_data);
                                    *io_data->cqe = *cqe;
                                    io_data->handle.destroy();
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
                this->usr_data_pool.deallocate(data);
            }
            io_uring_cq_advance(&ring, count);
        }
    }
    io_uring_queue_exit(&ring);
}