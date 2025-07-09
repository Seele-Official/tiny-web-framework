#include "coro_io_ctx.h"
#include "coro/threadpool.h"
#include "log.h"
#include <print>
using namespace seele;
using std::chrono::operator""ms;
void coro_io_ctx::worker(std::stop_token st){

    size_t submit_count = 0;
    while (!st.stop_requested()){

        if (unp_sem.try_acquire_for(50ms)){
            auto req = unprocessed_requests.pop_front();
            auto& [handle, cqe, is_timeout_link, time_out, helper_ptr, sqe_handle] = *req.value();
            if (is_timeout_link) {
                if (time_out) {
                    auto* sqe = io_uring_get_sqe(&ring);
                    auto* timeout_sqe = io_uring_get_sqe(&ring);

                    if (!sqe || !timeout_sqe) {
                        log::async().error("io_uring_get_sqe failed: {}", strerror(errno));
                        break;
                    }
                    
                    sqe_handle(helper_ptr, sqe);
                    sqe->user_data = std::bit_cast<std::uintptr_t>(req.value());                    
                    sqe->flags |= IOSQE_IO_LINK;
                    io_uring_prep_link_timeout(timeout_sqe, time_out, 0);
                    timeout_sqe->user_data = ETIMEDOUT;
                    submit_count += 2;
                } else {
                    log::async().error("Timeout link requested but time_out is null");
                }
            } else {
                auto* sqe = io_uring_get_sqe(&ring);
                if (!sqe) {
                    log::async().error("io_uring_get_sqe failed: {}", strerror(errno));
                    break;
                }
                sqe_handle(helper_ptr, sqe);
                sqe->user_data = std::bit_cast<std::uintptr_t>(req.value());
                submit_count++;      
            }
            if (submit_count >= submit_threshold) {
                auto submit_ret = io_uring_submit(&ring);
                if (submit_ret < 0) {
                    log::async().error("io_uring_submit failed: {}", strerror(-submit_ret));
                } else {
                    log::async().debug("Submitted {} requests to io_uring", submit_ret);
                    this->pending_sqe_count.fetch_add(submit_ret, std::memory_order_release);
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
                    this->pending_sqe_count.fetch_add(submit_ret, std::memory_order_release);
                    submit_count = 0;
                }
            }
        }
    }    
    

}




void coro_io_ctx::listener(std::stop_token st){
    while (!st.stop_requested()) {

        io_uring_cqe* cqe;
        int ret = io_uring_wait_cqe(&ring, &cqe);

        if (ret == -ETIME) {
            continue;
        } else if (ret < 0) {
            log::async().error("io_uring_wait_cqe_timeout failed: {}", strerror(-ret));
            continue;
        } else {
            if (cqe->user_data == ETIMEDOUT) {
                io_uring_cqe_seen(&ring, cqe);
                this->pending_sqe_count.fetch_sub(1, std::memory_order_release);
                continue; // Timeout, skip this cqe
            }

            auto* req = std::bit_cast<request*>(cqe->user_data);

            *req->cqe = *cqe;
            std::atomic_signal_fence( std::memory_order_release);

            coro::thread::dispatch(req->handle);
            delete req;
            io_uring_cqe_seen(&ring, cqe);
            this->pending_sqe_count.fetch_sub(1, std::memory_order_release);
        }
    }

    while (true) {
        auto ret = unprocessed_requests.pop_front();
        if (!ret.has_value()) {
            break; // No more requests to process
        }
        auto* req = ret.value();
        req->handle.destroy();
        delete req;
    }
}

coro_io_ctx::~coro_io_ctx() {
    __kernel_timespec ts{ .tv_sec = 1, .tv_nsec = 0 };
    io_uring_submit(&ring);


    while (pending_sqe_count.load() > 0) {
        std::println("Waiting for {} pending requests to complete...", pending_sqe_count.load());

        io_uring_cqe* cqe;
        int ret = io_uring_wait_cqe_timeout(&ring, &cqe, &ts);

        if (ret == -ETIME) {
            continue;
        } else if (ret < 0) {
            log::sync().error("io_uring_wait_cqe_timeout failed: {}", strerror(-ret));
            continue;
        } else {
            if (cqe->user_data == ETIMEDOUT) {
                io_uring_cqe_seen(&ring, cqe);
                this->pending_sqe_count.fetch_sub(1, std::memory_order_release);
                continue; // Timeout, skip this cqe
            }

            auto* req = std::bit_cast<request*>(cqe->user_data);

            *req->cqe = *cqe;
            std::atomic_signal_fence( std::memory_order_release);

            req->handle.destroy();
            delete req;
            io_uring_cqe_seen(&ring, cqe);
            this->pending_sqe_count.fetch_sub(1, std::memory_order_release);
        }
    }
    io_uring_queue_exit(&ring); 
}