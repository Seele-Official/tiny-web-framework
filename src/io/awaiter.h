#pragma once

#include "io/error.h"
#include "io/ctx.h"
#include <cstddef>
#include <cstdint>

namespace io::awaiter {

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
        std::atomic<int32_t> io_ret;
        std::coroutine_handle<> handle;
        bool await_ready() { return false; }
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> handle) {
            this->handle = handle;
            if(detail::ctx::get_instance().submit(
                    this, 
                    [](void* helper_ptr, io_uring* ring) {
                        return static_cast<decltype(this)>(helper_ptr)->init(ring);
                    }
                )
            ) {
                return std::noop_coroutine();
            }
            error::detail::set_msg("Coro ctx closed.");
            this->io_ret = error::CTX_CLOSED;
            return this->handle;
        }
        int32_t await_resume() {
            io_ret = this->io_ret.load(std::memory_order_acquire);
            if (io_ret < 0) {
                error::detail::set_code(io_ret);
                return error::SYS;
            }
            return io_ret; 
        }

        int init(io_uring* ring) {
            auto* sqe = io_uring_get_sqe(ring);
            static_cast<derived*>(this)->setup(sqe);
            sqe->user_data = std::bit_cast<std::uintptr_t>(
                detail::ctx::get_instance().new_usr_data(
                    detail::ctx::io_usr_data{
                        this->handle,
                        &this->io_ret
                    }
                )
            );
            return 1; // return the number of sqe written
        }

        void setup(io_uring_sqe*) { std::terminate();} // Default setup, can be overridden by derived classes
    };


    struct read : base<read> {
        int fd;
        void* buf;
        size_t len;
        size_t offset;

        read(int fd, void* buf, size_t len, size_t offset = 0)
            : fd(fd), buf(buf), len(len), offset(offset) {}

        void setup(io_uring_sqe* sqe) {
            io_uring_prep_read(sqe, fd, buf, len, offset);
        }
    };

    struct write : base<write> {
        int fd;
        const void* buf;
        unsigned int len;
        size_t offset;

        write(int fd, const void* buf, unsigned int len, size_t offset = 0)
            : fd(fd), buf(buf), len(len), offset(offset) {}

        void setup(io_uring_sqe* sqe) {
            io_uring_prep_write(sqe, fd, buf, len, offset);
        }
    };

    struct readv : base<readv> {
        int fd;
        const iovec* iov;
        uint32_t nr_iov;
        size_t offset;

        readv(int fd, const iovec* iov, uint32_t nr_iov = 1, size_t offset = 0)
            : fd(fd), iov(iov), nr_iov(nr_iov), offset(offset) {}

        void setup(io_uring_sqe* sqe) {
            io_uring_prep_readv(sqe, fd, iov, nr_iov, offset);
        }
    };

    struct writev : base<writev> {
        int fd;
        const iovec* iov;
        uint32_t nr_iov;
        size_t offset;
        writev() = default;
        writev(int fd, const iovec* iov, uint32_t nr_iov, size_t offset = 0)
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

    struct read_direct : base<read_direct> {
        int fd_index;
        void* buf;
        size_t len;
        size_t offset;

        read_direct(int fd_index, void* buf, size_t len, size_t offset = 0)
            : fd_index(fd_index), buf(buf), len(len), offset(offset) {}

        void setup(io_uring_sqe* sqe) {
            io_uring_prep_read(sqe, fd_index, buf, len, offset);
            sqe->flags |= IOSQE_FIXED_FILE;
        }
    };

    struct write_direct : base<write_direct> {
        int fd_index;
        const void* buf;
        unsigned int len;
        size_t offset;

        write_direct(int fd_index, const void* buf, unsigned int len, size_t offset = 0)
            : fd_index(fd_index), buf(buf), len(len), offset(offset) {}

        void setup(io_uring_sqe* sqe) {
            io_uring_prep_write(sqe, fd_index, buf, len, offset);
            sqe->flags |= IOSQE_FIXED_FILE;
        }
    };

    struct writev_direct : base<writev_direct> {
        int fd_index;
        const iovec* iov;
        uint32_t nr_iov;
        size_t offset;

        writev_direct(int fd_index, const iovec* iov, uint32_t nr_iov, size_t offset = 0)
            : fd_index(fd_index), iov(iov), nr_iov(nr_iov), offset(offset) {}

        void setup(io_uring_sqe* sqe) {
            io_uring_prep_writev(sqe, fd_index, iov, nr_iov, offset);
            sqe->flags |= IOSQE_FIXED_FILE;
        }
    };

    struct accept_direct : base<accept_direct> {
        int fd;
        sockaddr* addr;
        socklen_t* addrlen;
        int flags;
        unsigned int file_index;
        accept_direct(int fd, sockaddr* addr, socklen_t* addrlen, int flags = 0, unsigned int file_index = IORING_FILE_INDEX_ALLOC)
            : fd(fd), addr(addr), addrlen(addrlen), flags(flags), file_index(file_index) {}
        void setup(io_uring_sqe* sqe) {
            io_uring_prep_accept_direct(sqe, fd, addr, addrlen, flags, file_index);
        }
    };

    struct close_direct : base<close_direct> {
        int fd_index;
        close_direct(int fd_index) : fd_index(fd_index) {}
        void setup(io_uring_sqe* sqe) {
            io_uring_prep_close_direct(sqe, fd_index);
        }
    };

    struct cancel_fd : base<cancel_fd> {
        int fd;
        cancel_fd(int fd) : fd(fd) {}
        void setup(io_uring_sqe* sqe) {
            io_uring_prep_cancel_fd(sqe, fd, 0);
        }
    };


    template <typename T, template <typename...> class Template>
    struct is_specialization_of : std::false_type {};

    template <template <typename...> class Template, typename... Args>
    struct is_specialization_of<Template<Args...>, Template> : std::true_type {};

    template <typename T, template <typename...> class Template>
    constexpr bool is_specialization_of_v = is_specialization_of<T, Template>::value;
    
    struct kernel_ts : __kernel_timespec {
        template<typename duration_t>
            requires is_specialization_of_v<std::decay_t<duration_t>, std::chrono::duration>
        kernel_ts (duration_t&& duration) {
            this->tv_sec = std::chrono::duration_cast<std::chrono::seconds>(duration).count();
            this->tv_nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(
                duration - std::chrono::seconds(this->tv_sec)).count();
        }
    };

    struct time_out : base<time_out> {
        kernel_ts ts;
        time_out(kernel_ts ts) : ts(ts) {}
        void setup(io_uring_sqe* sqe) {
            io_uring_prep_timeout(sqe, &ts, 0, 0);
        }
    };


    template<typename io_awaiter_t>
        requires std::is_base_of_v<base<io_awaiter_t>, io_awaiter_t>
    struct link_timeout {
        io_awaiter_t awaiter;
        kernel_ts ts;
        bool await_ready() { return false; }
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> handle) {
            this->awaiter.handle = handle;
            if(detail::ctx::get_instance().submit(
                    this, 
                    [](void* helper_ptr, io_uring* ring) {
                        return static_cast<decltype(this)>(helper_ptr)->init(ring);
                    }
                )
            ) {
                return std::noop_coroutine();
            }
            error::detail::set_msg("Coro ctx closed.");
            this->awaiter.io_ret = error::CTX_CLOSED;
            return this->awaiter.handle;
        }
        int init(io_uring* ring) {
            auto* sqe = io_uring_get_sqe(ring);
            auto* timeout_sqe = io_uring_get_sqe(ring);
            // Need to handle validation of sqe, but we assume the it's valid
            this->awaiter.setup(sqe);
            auto io_data = detail::ctx::get_instance().new_usr_data(
                std::in_place_type<detail::ctx::io_usr_data>,
                this->awaiter.handle,
                &this->awaiter.io_ret
            );  

            sqe->user_data = std::bit_cast<std::uintptr_t>(io_data);

            sqe->flags |= IOSQE_IO_LINK;

            io_uring_prep_link_timeout(timeout_sqe, &this->ts, 0);

            timeout_sqe->user_data = std::bit_cast<std::uintptr_t>(
                detail::ctx::get_instance().new_usr_data(
                    detail::ctx::timeout_usr_data{
                        io_data
                    }
                )
            );
            return 2;
        }

        int32_t await_resume() { 
            auto io_ret = this->awaiter.io_ret.load(std::memory_order_acquire);
            if (io_ret < 0){
                if (io_ret == -ECANCELED) {
                    error::detail::set_msg("Time out.");
                    return error::TIMEOUT;
                }
                error::detail::set_code(io_ret);
                return error::SYS;
            }
            return io_ret;
        }
    };



} // namespace io::awaiter
