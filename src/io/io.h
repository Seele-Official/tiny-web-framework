#pragma once
#include <cstdint>
#include <cstring>
#include <ctime>

#include <liburing.h>
#include <liburing/io_uring.h>


#include "io/ctx.h"
#include "io/error.h"
#include "io/awaiter.h"

namespace io {
    
using namespace seele;

inline void run() {
    detail::ctx::get_instance().run();
}

inline void request_stop() {
    detail::ctx::get_instance().request_stop();
}

inline int32_t register_files(const int32_t* fds, uint32_t count){
    return detail::ctx::get_instance().register_files(fds, count);
}
inline int32_t unregister_files() {
    return detail::ctx::get_instance().unregister_files();
}
inline int32_t register_file_alloc_range(uint32_t off, uint32_t len) {
    return detail::ctx::get_instance().register_file_alloc_range(off, len);
}
inline int32_t register_files_sparse(uint32_t count) {
    return detail::ctx::get_instance().register_files_sparse(count);
}


struct fd_wrapper {
    
    fd_wrapper() = default;
    
    fd_wrapper(int fd) : fd(fd) {}
    fd_wrapper(fd_wrapper&) = delete;
    fd_wrapper(fd_wrapper&& other) noexcept : fd(other.fd) {
        other.fd = -1; // Prevent the destructor from closing the fd
    }
    fd_wrapper& operator=(fd_wrapper&) = delete;
    fd_wrapper& operator=(fd_wrapper&& other) noexcept {
        if (this != &other) {
            if (this->is_valid()) {
                close(fd);
            }
            fd = other.fd;
            other.fd = -1; // Prevent the destructor from closing the fd
        }
        return *this;
    }
    int get() const {
        return fd;
    }
    int release() {
        int temp = fd;
        fd = -1; // Prevent the destructor from closing the fd
        return temp;
    }
    bool is_valid() const {
        return fd >= 0;
    }
    ~fd_wrapper() {
        if (this->is_valid()) {
            close(fd);
        }
    }
    
    int fd{-1};
};
} // namespace io


