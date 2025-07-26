#pragma once
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <liburing.h>
#include <liburing/io_uring.h>
#include <new>
#include <optional>
#include <tuple>
#include <errno.h>
#include <type_traits>
#include "meta.h"
#include "coro_io_ctx.h"


namespace coro_io::error{
    constexpr int32_t SYS = -1;
    constexpr int32_t CTX_CLOSED = -2;
    constexpr int32_t TIMEOUT = -3;
    
    inline thread_local std::string_view msg = "";  

    inline void set(std::string_view e){
        msg = e;
    }
}

namespace coro_io::awaiter {

    



    // Because in higher versions of liburing, support CQE32 feature
    // CQE32 has flexible array member, which can not be used as a member of a class
    // So we use a wrapper to hold the cqe, and use placement new to construct
    // the cqe in the storage.
    struct cqe_wrapper{
        alignas(io_uring_cqe) std::byte storage[32];
        cqe_wrapper() {
            new (storage) io_uring_cqe(); 
        }
        inline io_uring_cqe* cqe_ptr() {
            return std::launder(reinterpret_cast<io_uring_cqe*>(storage));
        }

        inline io_uring_cqe* operator->() {
            return this->cqe_ptr();
        }
    };



    template<typename derived>
    struct base {
        int32_t io_ret;
        std::coroutine_handle<> handle;
        bool await_ready() { return false; }
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> handle) {
            this->handle = handle;
            if(ctx::get_instance().submit(
                    this, 
                    [](void* helper_ptr, io_uring* ring) {
                        return static_cast<decltype(this)>(helper_ptr)->init(ring);
                    }
                )
            ) {
                return std::noop_coroutine();
            }
            error::set("Coro ctx closed.");
            this->io_ret = error::CTX_CLOSED;
            return this->handle;
        }
        int32_t await_resume() {
            if (io_ret < 0) {
                error::set(strerror(-io_ret));
                return error::SYS;
            }
            return io_ret; 
        }

        int init(io_uring* ring) {
            auto* sqe = io_uring_get_sqe(ring);
            static_cast<derived*>(this)->setup(sqe);
            sqe->user_data = std::bit_cast<std::uintptr_t>(
                ctx::get_instance().new_usr_data(
                    ctx::io_usr_data{
                        this->handle,
                        &this->io_ret
                    }
                )
            );
            return 1;
        }

        void setup(io_uring_sqe* sqe) { std::terminate();} // Default setup, can be overridden by derived classes
    };


    struct read : base<read> {
        int fd;
        void* buf;
        size_t len;
        off_t offset;

        read(int fd, void* buf, size_t len, off_t offset = 0)
            : fd(fd), buf(buf), len(len), offset(offset) {}

        void setup(io_uring_sqe* sqe) {
            io_uring_prep_read(sqe, fd, buf, len, offset);
        }
    };


    struct write : base<write> {
        int fd;
        const void* buf;
        unsigned int len;
        off_t offset;

        write(int fd, const void* buf, unsigned int len, off_t offset = 0)
            : fd(fd), buf(buf), len(len), offset(offset) {}

        void setup(io_uring_sqe* sqe) {
            io_uring_prep_write(sqe, fd, buf, len, offset);
        }
    };


    struct readv : base<readv> {
        int fd;
        const iovec* iov;
        unsigned nr_iov;
        off_t offset;

        readv(int fd, const iovec* iov, unsigned nr_iov = 1, off_t offset = 0)
            : fd(fd), iov(iov), nr_iov(nr_iov), offset(offset) {}

        void setup(io_uring_sqe* sqe) {
            io_uring_prep_readv(sqe, fd, iov, nr_iov, offset);
        }
    };



    struct writev : base<writev> {
        int fd;
        const iovec* iov;
        unsigned nr_iov;
        off_t offset;
        writev() = default;
        writev(int fd, const iovec* iov, unsigned nr_iov, off_t offset = 0)
            : fd(fd), iov(iov), nr_iov(nr_iov), offset(offset) {}

        void setup(io_uring_sqe* sqe) {
            io_uring_prep_writev(sqe, fd, iov, nr_iov, offset);
        }
    };



    struct accept : base<accept> {
        int fd;
        sockaddr* addr;
        socklen_t* addrlen;
        int flags;
        accept(int fd, sockaddr* addr, socklen_t* addrlen, int flags = 0)
            : fd(fd), addr(addr), addrlen(addrlen), flags(flags) {}
        void setup(io_uring_sqe* sqe) {
            io_uring_prep_accept(sqe, fd, addr, addrlen, flags);
        }
    };





    template<typename io_awaiter_t>
        requires std::is_base_of_v<base<io_awaiter_t>, io_awaiter_t>
    struct link_timeout {
        __kernel_timespec ts;
        io_awaiter_t awaiter;
        bool await_ready() { return false; }
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> handle) {
            this->awaiter.handle = handle;
            if(ctx::get_instance().submit(
                    this, 
                    [](void* helper_ptr, io_uring* ring) {
                        return static_cast<decltype(this)>(helper_ptr)->init(ring);
                    }
                )
            ) {
                return std::noop_coroutine();
            }
            error::set("Coro ctx closed.");
            this->awaiter.io_ret = error::CTX_CLOSED;
            return this->awaiter.handle;
        }
        int init(io_uring* ring) {
            auto* sqe = io_uring_get_sqe(ring);
            auto* timeout_sqe = io_uring_get_sqe(ring);
            // Need to handle validation of sqe, but we assume the it's valid
            this->awaiter.setup(sqe);
            auto io_data = ctx::get_instance().new_usr_data(
                ctx::io_usr_data{
                    this->awaiter.handle,
                    &this->awaiter.io_ret
                }
            );  

            sqe->user_data = std::bit_cast<std::uintptr_t>(io_data);

            sqe->flags |= IOSQE_IO_LINK;

            io_uring_prep_link_timeout(timeout_sqe, &this->ts, 0);

            timeout_sqe->user_data = std::bit_cast<std::uintptr_t>(
                ctx::get_instance().new_usr_data(
                    ctx::timeout_usr_data{
                        io_data
                    }
                )
            );
            return 2;
        }

        int32_t await_resume() { 
            if (awaiter.io_ret < 0){
                if (awaiter.io_ret == -ECANCELED) {
                    error::set("Time out.");
                    return error::TIMEOUT;
                }
                error::set(strerror(-awaiter.io_ret));
                return error::SYS;
            }
            return awaiter.io_ret;
        }
        link_timeout() = default;


        template<typename duration_t>
            requires seele::meta::is_specialization_of_v<std::decay_t<duration_t>, std::chrono::duration>
        link_timeout(io_awaiter_t&& awaiter, duration_t&& duration) : 
            ts(std::chrono::duration_cast<std::chrono::seconds>(duration).count(),
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                duration - std::chrono::seconds(ts.tv_sec)).count()),
            awaiter(std::forward<io_awaiter_t>(awaiter)){}

    };

    template<typename io_awaiter_t, typename duration_t>
    link_timeout(io_awaiter_t&&, duration_t&&) -> link_timeout<std::decay_t<io_awaiter_t>>;


    template<typename function_t, typename... args_t>
        requires std::is_invocable_v<function_t, io_uring_sqe*, args_t...>
    struct any : base<any<function_t, args_t...>> {
        std::tuple<args_t...> args;
        function_t func;
        any(function_t&& func, args_t&&... args)
            : args(std::forward<args_t>(args)...), func(std::forward<function_t>(func)) {}

        void setup(io_uring_sqe* sqe) {
            // This is a placeholder, actual implementation would depend on the function type
            // and how it interacts with io_uring.
        }
    };
}
