#pragma once
#include <bit>
#include <print>
#include <cstdint>
#include <cstring>
#include <cstddef>



#include <unistd.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/sendfile.h>
#include <sys/mman.h>


#include "net/ipv4.h"





struct fd_wrapper {
    int fd;
    fd_wrapper() : fd(-1) {}
    fd_wrapper(int fd) : fd(fd) {}
    fd_wrapper(fd_wrapper&) = delete;
    fd_wrapper(fd_wrapper&& other) noexcept : fd(other.fd) {
        other.fd = -1; // Prevent the destructor from closing the fd
    }
    fd_wrapper& operator=(fd_wrapper&) = delete;
    fd_wrapper& operator=(fd_wrapper&& other) noexcept {
        if (this != &other) {
            if (is_valid()) {
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
        if (is_valid()) {
            close(fd);
        }
    }
};

struct iovec_wrapper : iovec {
    iovec_wrapper() : iovec{nullptr, 0} {}
    iovec_wrapper(size_t len) : iovec{new std::byte[len], len} {}
    iovec_wrapper(iovec_wrapper&) = delete;
    iovec_wrapper(iovec_wrapper&& other) noexcept : iovec{other.iov_base, other.iov_len} {
        other.iov_base = nullptr;
        other.iov_len = 0;
    }
    iovec_wrapper& operator=(iovec_wrapper&) = delete;
    iovec_wrapper& operator=(iovec_wrapper&& other) noexcept {
        if (this != &other) {
            delete[] static_cast<std::byte*>(iov_base);
            iov_base = other.iov_base;
            iov_len = other.iov_len;
            other.iov_base = nullptr;
            other.iov_len = 0;
        }
        return *this;
    }

    ~iovec_wrapper() {
        if (iov_base){
            delete[] static_cast<std::byte*>(iov_base);
        }
    }
};


struct mmap_wrapper{
    size_t size;
    void* data;
    mmap_wrapper() : size(0), data(nullptr) {}
    mmap_wrapper(size_t length, int prot, int flags, int fd, off_t offset = 0) 
        : size(length), data(mmap(nullptr, length, prot, flags, fd, offset)) {
        if (data == MAP_FAILED) {
            std::println("Failed to mmap file: {}", strerror(errno));
            std::println("Parameters: length={}, prot={}, flags={}, fd={}, offset={}", 
                         length, prot, flags, fd, offset);
            std::terminate();
        }
    }

    mmap_wrapper(const mmap_wrapper&) = delete;
    mmap_wrapper(mmap_wrapper&& other) noexcept : size(other.size), data(other.data) {
        other.data = nullptr;
        other.size = 0;
    }
    mmap_wrapper& operator=(mmap_wrapper&& other) noexcept {
        if (this != &other) {
            if (data) {
                munmap(data, size);
            }
            size = other.size;
            data = other.data;
            other.data = nullptr;
            other.size = 0;
        }
        return *this;
    }

    mmap_wrapper& operator=(const mmap_wrapper&) = delete;

    ~mmap_wrapper(){
        if (data) {
            munmap(data, size);
        }
    }
};


fd_wrapper setup_socket(seele::net::ipv4 v4, size_t max_connections, auto (*setup)(int) -> void);

int64_t get_file_size(const fd_wrapper& fd_w);