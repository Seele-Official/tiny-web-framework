#include "io_ctx.h"
#include "coro/threadpool.h"
#include "log.h"
#include <atomic>
#include <cstring>
#include <print>
using namespace seele;

void io_ctx::worker(std::stop_token stop_token){
    __kernel_timespec ts{ .tv_sec = 5, .tv_nsec = 0};

    while (!stop_token.stop_requested()) {

        io_uring_cqe* cqe;
        int ret = io_uring_wait_cqe_timeout(&ring, &cqe, &ts);

        if (ret == -ETIME) {
            continue;
        } else if (ret < 0) {
            log::async().error("io_uring_wait_cqe_timeout failed: {}", strerror(-ret));
            continue;
        } else {
            if (cqe->user_data == ETIMEDOUT) {
                io_uring_cqe_seen(&ring, cqe);
                continue; // Timeout, skip this cqe
            }

            auto* req = std::bit_cast<request*>(cqe->user_data);

            *req->cqe = *cqe;
            std::atomic_signal_fence( std::memory_order_release);

            coro::thread::dispatch(req->handle);
            delete req;
            io_uring_cqe_seen(&ring, cqe);
            this->pending_requests.fetch_sub(1, std::memory_order_release);
        }
    }
}

io_ctx::~io_ctx() {
    __kernel_timespec ts{ .tv_sec = 1, .tv_nsec = 0 };
    while (pending_requests.load() > 0) {
        std::println("Waiting for {} pending requests to complete...", pending_requests.load());

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
                continue; // Timeout, skip this cqe
            }

            auto* req = std::bit_cast<request*>(cqe->user_data);

            *req->cqe = *cqe;
            std::atomic_signal_fence( std::memory_order_release);

            req->handle.destroy();
            delete req;
            io_uring_cqe_seen(&ring, cqe);
            this->pending_requests.fetch_sub(1, std::memory_order_release);
        }
    }
    io_uring_queue_exit(&ring); 
}