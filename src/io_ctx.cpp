#include "io_ctx.h"
#include "coro/threadpool.h"
#include "log.h"
#include <atomic>
#include <cstring>
#include <mutex>
#include <print>
using namespace seele;

void io_ctx::worker(std::stop_token stop_token){
    __kernel_timespec ts{ .tv_sec = 0, .tv_nsec = 50'000'000 };

    while (!stop_token.stop_requested()) {

        io_uring_cqe* cqe;
        int ret = io_uring_wait_cqe_timeout(&ring, &cqe, &ts);

        if (ret == -ETIME) {
            if (this->unprocessed_sqe_count.load(std::memory_order_acquire) > 0 && this->unprocessed_sqe_count.load(std::memory_order_acquire) < submit_threshold) {   
                int sum_submit = 0;
                {
                    std::lock_guard<std::mutex> lock(ring_m);
                    sum_submit = io_uring_submit(&ring);
                }
                if (sum_submit >= 0){
                    this->pending_request_count.fetch_add(sum_submit, std::memory_order_release);
                    this->unprocessed_sqe_count.fetch_sub(sum_submit, std::memory_order_release);
                    log::async().debug("Submitted {} SQEs", sum_submit);
                } else {
                    std::print("io_uring_submit failed: {}\n", strerror(-sum_submit));
                }
            }
            continue;
        } else if (ret < 0) {
            log::async().error("io_uring_wait_cqe_timeout failed: {}", strerror(-ret));
            continue;
        } else {
            if (cqe->user_data == ETIMEDOUT) {
                io_uring_cqe_seen(&ring, cqe);
                this->pending_request_count.fetch_sub(1, std::memory_order_release);
                continue; // Timeout, skip this cqe
            }

            auto* req = std::bit_cast<request*>(cqe->user_data);

            *req->cqe = *cqe;
            std::atomic_signal_fence( std::memory_order_release);

            coro::thread::dispatch(req->handle);
            delete req;
            io_uring_cqe_seen(&ring, cqe);
            this->pending_request_count.fetch_sub(1, std::memory_order_release);
        }
    }
}

io_ctx::~io_ctx() {
    __kernel_timespec ts{ .tv_sec = 1, .tv_nsec = 0 };
    {
        std::lock_guard<std::mutex> lock(ring_m);
        if (this->unprocessed_sqe_count.load(std::memory_order_acquire) > 0) {
            io_uring_submit(&ring);
            std::println("Submitting {} unprocessed SQEs before shutdown", this->unprocessed_sqe_count.load(std::memory_order_acquire));
            this->pending_request_count.fetch_add(this->unprocessed_sqe_count.load(std::memory_order_acquire), std::memory_order_release);
            this->unprocessed_sqe_count.store(0, std::memory_order_release);
        }
    }
    while (pending_request_count.load() > 0) {
        std::println("Waiting for {} pending requests to complete...", pending_request_count.load());

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
                this->pending_request_count.fetch_sub(1, std::memory_order_release);
                continue; // Timeout, skip this cqe
            }

            auto* req = std::bit_cast<request*>(cqe->user_data);

            *req->cqe = *cqe;
            std::atomic_signal_fence( std::memory_order_release);

            req->handle.destroy();
            delete req;
            io_uring_cqe_seen(&ring, cqe);
            this->pending_request_count.fetch_sub(1, std::memory_order_release);
        }
    }
    io_uring_queue_exit(&ring); 
}