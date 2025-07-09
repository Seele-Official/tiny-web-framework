#pragma once
#include <coroutine>
#include <expected>
#include <liburing.h>
#include <tuple>
#include <errno.h>
#include <type_traits>
#include "meta.h"
#include "coro_io_ctx.h"
template<typename derived>
struct io_awaiter {
    io_uring_cqe cqe;
    bool await_ready() { return false; }
    void await_suspend(std::coroutine_handle<void> handle) {
        coro_io_ctx::get_instance().submit(
           new coro_io_ctx::request{
                handle,
                &cqe,
                false, 
                nullptr, 
                this, 
                [](void* helper_ptr, io_uring_sqe* sqe) {
                    static_cast<derived*>(helper_ptr)->setup(sqe);
                }
            } 
        );
    }
    io_uring_cqe await_resume() { return cqe; }

    void setup(io_uring_sqe* sqe) { std::terminate();} // Default setup, can be overridden by derived classes
};


struct io_read_awaiter : io_awaiter<io_read_awaiter> {
    int fd;
    void* buf;
    size_t len;
    off_t offset;

    io_read_awaiter(int fd, void* buf, size_t len, off_t offset = 0)
        : fd(fd), buf(buf), len(len), offset(offset) {}

    void setup(io_uring_sqe* sqe) {
        io_uring_prep_read(sqe, fd, buf, len, offset);
    }
};


struct io_write_awaiter : io_awaiter<io_write_awaiter> {
    int fd;
    const void* buf;
    unsigned int len;
    off_t offset;

    io_write_awaiter(int fd, const void* buf, unsigned int len, off_t offset = 0)
        : fd(fd), buf(buf), len(len), offset(offset) {}

    void setup(io_uring_sqe* sqe) {
        io_uring_prep_write(sqe, fd, buf, len, offset);
    }
};


struct io_readv_awaiter : io_awaiter<io_readv_awaiter> {
    int fd;
    const iovec* iov;
    unsigned nr_iov;
    off_t offset;

    io_readv_awaiter(int fd, const iovec* iov, unsigned nr_iov = 1, off_t offset = 0)
        : fd(fd), iov(iov), nr_iov(nr_iov), offset(offset) {}

    void setup(io_uring_sqe* sqe) {
        io_uring_prep_readv(sqe, fd, iov, nr_iov, offset);
    }
};



struct io_writev_awaiter : io_awaiter<io_writev_awaiter> {
    int fd;
    const iovec* iov;
    unsigned nr_iov;
    off_t offset;
    io_writev_awaiter() = default;
    io_writev_awaiter(int fd, const iovec* iov, unsigned nr_iov, off_t offset = 0)
        : fd(fd), iov(iov), nr_iov(nr_iov), offset(offset) {}

    void setup(io_uring_sqe* sqe) {
        io_uring_prep_writev(sqe, fd, iov, nr_iov, offset);
    }
};



struct io_accept_awaiter : io_awaiter<io_accept_awaiter> {
    int fd;
    sockaddr* addr;
    socklen_t* addrlen;
    int flags;
    io_accept_awaiter(int fd, sockaddr* addr, socklen_t* addrlen, int flags = 0)
        : fd(fd), addr(addr), addrlen(addrlen), flags(flags) {}
    void setup(io_uring_sqe* sqe) {
        io_uring_prep_accept(sqe, fd, addr, addrlen, flags);
    }
};




struct io_time_out_error{};

template<typename io_awaiter_t>
    requires std::is_base_of_v<io_awaiter<io_awaiter_t>, io_awaiter_t>
struct io_link_timeout_awaiter {
    __kernel_timespec ts;
    io_awaiter_t awaiter;
    bool await_ready() { return false; }
    void await_suspend(std::coroutine_handle<void> handle) {
        coro_io_ctx::get_instance().submit(
            new coro_io_ctx::request{
                handle,
                &awaiter.cqe,
                true, 
                &ts, 
                &awaiter, 
                [](void* helper_ptr, io_uring_sqe* sqe) {
                    static_cast<io_awaiter_t*>(helper_ptr)->setup(sqe);
                }
            }
        );
    }
    std::expected<io_uring_cqe, io_time_out_error> await_resume() { 
        if (awaiter.cqe.res == -ECANCELED) {
            return std::unexpected(io_time_out_error{});
        }
        return awaiter.cqe;
    }
    io_link_timeout_awaiter() = default;


    template<typename duration_t>
        requires seele::meta::is_specialization_of_v<std::decay_t<duration_t>, std::chrono::duration>
    io_link_timeout_awaiter(io_awaiter_t&& awaiter, duration_t&& duration) : 
        ts(std::chrono::duration_cast<std::chrono::seconds>(duration).count(),
           std::chrono::duration_cast<std::chrono::nanoseconds>(
               duration - std::chrono::seconds(ts.tv_sec)).count()),
        awaiter(std::forward<io_awaiter_t>(awaiter)){}

};

template<typename io_awaiter_t, typename duration_t>
io_link_timeout_awaiter(io_awaiter_t&&, duration_t&&) -> io_link_timeout_awaiter<std::decay_t<io_awaiter_t>>;


template<typename function_t, typename... args_t>
    requires std::is_invocable_v<function_t, io_uring_sqe*, args_t...>
struct io_any_awaiter : io_awaiter<io_any_awaiter<function_t, args_t...>> {
    std::tuple<args_t...> args;
    function_t func;
    io_any_awaiter(function_t&& func, args_t&&... args)
        : args(std::forward<args_t>(args)...), func(std::forward<function_t>(func)) {}

    void setup(io_uring_sqe* sqe) {
        // This is a placeholder, actual implementation would depend on the function type
        // and how it interacts with io_uring.
    }
};