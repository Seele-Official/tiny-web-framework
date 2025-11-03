#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>

#include <filesystem>
#include <liburing.h>
#include <liburing/io_uring.h>

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

#include "io/ctx.h"
#include "io/error.h"
#include "io/awaiter.h"

namespace io {
    

inline void run() {
    detail::ctx::get_instance().run();
}

inline void clean_up() {
    detail::ctx::get_instance().clean_up();
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


class fd {
public:
    fd() = default;
    fd(int fd) : m_fd(fd) {}
    fd(fd&) = delete;
    fd(fd&& other) : fd(other.m_fd) {
        other.m_fd = -1; 
    }
    fd& operator=(fd&) = delete;
    fd& operator=(fd&& other) {
        if (this != &other) {
            if (this->is_valid()) {
                close(m_fd);
            }
            m_fd = other.m_fd;
            other.m_fd = -1;
        }
        return *this;
    }   

    ~fd() {
        if (this->is_valid()) {
            close(m_fd);
        }
    }

    int get() const {
        return m_fd;
    }
    int release() {
        int temp = m_fd;
        m_fd = -1;
        return temp;
    }
    bool is_valid() const {
        return m_fd >= 0;
    }

    static fd open_file(const std::filesystem::path& path, int flag){
        return io::fd{open(path.c_str(), flag)};
    }
    
    static fd open_socket(sockaddr_in addr, size_t max_connections, auto (*setup)(int) -> void) {
        int fd = socket(PF_INET, SOCK_STREAM, 0);
        setup(fd);
        if (bind(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
            return -1;
        }
        if (listen(fd, max_connections) < 0) {
            return -1;
        }
        return fd;
    }


private:
    int m_fd{-1};
};


class mmap{
public:
    mmap() = default;
    mmap(size_t length, int prot, int flags, int fd, off_t offset = 0) 
        : size(length), data(static_cast<std::byte*>(::mmap(nullptr, length, prot, flags, fd, offset))) {
        if (data == MAP_FAILED) {
            std::println("Failed to mmap file: {}", strerror(errno));
            std::println("Parameters: length={}, prot={}, flags={}, fd={}, offset={}", 
                         length, prot, flags, fd, offset);
            std::terminate();
        }
    }

    mmap(const mmap&) = delete;
    mmap(mmap&& other) : size(other.size), data(other.data) {
        other.data = nullptr;
        other.size = 0;
    }
    mmap& operator=(mmap&& other) {
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

    mmap& operator=(const mmap&) = delete;

    ~mmap(){
        if (data) {
            munmap(data, size);
        }
    }
    
    size_t      get_size() const { return this->size; };
    std::byte*  get_data() const { return this->data; }
private:
    size_t      size{0};
    std::byte*  data{nullptr};
};

} // namespace io


