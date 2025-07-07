#include <algorithm>
#include <bits/types/struct_iovec.h>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <exception>
#include <expected>
#include <format>
#include <mutex>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <cstddef>
#include <unordered_map>
#include <utility>
#include <vector>
#include <filesystem>

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/sendfile.h>
#include <sys/mman.h>

#include "meta.h"
#include "net/ipv4.h"
#include "coro/task.h"
#include "coro/async.h"
#include "log.h"
#include "http.h"
#include "coro_io.h"
using std::literals::operator""s;
using std::literals::operator""ms;


struct fd_wrapper {
    int fd;
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
    operator int() const {
        return fd;
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


struct file_mmap{
    size_t size;
    void* data;
    file_mmap(size_t length, int prot = PROT_READ | PROT_WRITE,
                      int flags = MAP_SHARED | MAP_ANONYMOUS, int fd = -1,
                      off_t offset = 0) : size(length), data(mmap(nullptr, length, prot, flags, fd, offset)) {
        if (data == MAP_FAILED) {
            std::println("Failed to mmap file: {}", strerror(errno));
            std::terminate();
        }
    }

    file_mmap(const file_mmap&) = delete;
    file_mmap(file_mmap&& other) noexcept : size(other.size), data(other.data) {
        other.data = nullptr;
        other.size = 0;
    }
    file_mmap& operator=(file_mmap&& other) noexcept {
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

    file_mmap& operator=(const file_mmap&) = delete;

    ~file_mmap(){
        if (data) {
            munmap(data, size);
        }
    }
};


fd_wrapper setup_socket(net::ipv4 v4){
    fd_wrapper fd_w = socket(PF_INET, SOCK_STREAM, 0);
    sockaddr_in addr = v4.to_sockaddr_in();
    if (bind(fd_w, (sockaddr*)&addr, sizeof(addr)) < 0) {
        return fd_wrapper(-1);
    }
    if (listen(fd_w, SOMAXCONN) < 0) {
        return fd_wrapper(-1);
    }
    return fd_w;
}

int64_t get_file_size(const fd_wrapper& fd_w) {
    struct stat st;

    if(fstat(fd_w, &st) < 0) {
        return -1;
    }
    if (S_ISBLK(st.st_mode)) {
        int64_t bytes;
        if (ioctl(fd_w, BLKGETSIZE64, &bytes) != 0) {
            return -1;
        }
        return bytes;
    } else if (S_ISREG(st.st_mode))
        return st.st_size;

    return -1;
}




struct send_http_error : io_link_timeout_awaiter<io_write_awaiter> {
    send_http_error(int fd, http::error_code code) 
        : io_link_timeout_awaiter<io_write_awaiter>{
            io_write_awaiter{fd, http::error_contents[code].data(), meta::safe_cast<unsigned int>(http::error_contents[code].size())},
            5s
        } {}
};





std::string_view default_path{};

std::mutex file_caches_mutex{};
std::unordered_map<std::string, file_mmap> file_caches{};


struct get_res{
    iovec_wrapper header;
    iovec data;
};

std::expected<get_res, http::error_code> handle_get_req(const http::req_msg& req) {

    std::filesystem::path uri_path = req.req_l.uri;
    if (uri_path.empty() || uri_path == "/") {
        uri_path = "/index.html";
    }
    
    uri_path = uri_path.lexically_normal();


    if (!uri_path.is_absolute() || 
        uri_path.string().find("..") != std::string::npos) { return std::unexpected{http::error_code::forbidden};
    }
    

    std::filesystem::path root_path(default_path);
    std::filesystem::path full_path = root_path / uri_path.relative_path();
    full_path = std::filesystem::weakly_canonical(full_path);

    if (std::filesystem::is_directory(full_path)) {
        full_path /= "index.html";
    }

    if (full_path.string().find(root_path.string()) != 0
        || std::filesystem::is_directory(full_path)
        || !std::filesystem::is_regular_file(full_path)
        ) {
        return std::unexpected{http::error_code::forbidden};
    }


    fd_wrapper file_fd_w(open(full_path.c_str(), O_RDONLY));
    if (!file_fd_w.is_valid()) {
        if (errno == EACCES || errno == EPERM) {
            return std::unexpected{http::error_code::forbidden};
        }
        return std::unexpected{http::error_code::not_found};
    }


    int64_t file_size = get_file_size(file_fd_w);
    if (file_size < 0) {
        log::async().error("Failed to get file size for {}", full_path.string());
        return std::unexpected{http::error_code::internal_server_error};
    }

    get_res res{
        {256},
        {}
    };
    {
        std::lock_guard<std::mutex> lock(file_caches_mutex);
        auto it = file_caches.find(full_path.string());
        if (it != file_caches.end()) {
            res.data.iov_base = it->second.data;
            res.data.iov_len = it->second.size;
        } else {
            file_caches.emplace(
                full_path.string(),
                file_mmap(file_size, PROT_READ, MAP_SHARED, file_fd_w, 0)
            );
        }
    }

    std::string content_type = "application/octet-stream";
    std::string ext = full_path.extension().string();

    if (auto it = http::mime_types.find(ext);it != http::mime_types.end()) {
        content_type = it->second;
    }
    

    http::res_msg res_msg{
        {
            .status_code = 200,
            .version = "HTTP/1.1",
            .reason_phrase = "OK"
        },
        {
            {"Content-Length", std::to_string(file_size)},
            {"Content-Type", content_type},
            {"X-Content-Type-Options", "nosniff"} 
        },
        std::nullopt
    };



    res.header.iov_len = meta::safe_cast<size_t>(
        res_msg.format_to(static_cast<char*>(res.header.iov_base))
         - static_cast<char*>(res.header.iov_base)
    );
    return res;
}



coro::task async_handle_connection(int fd, net::ipv4 addr) {
    fd_wrapper fd_w(fd);
    net::ipv4 client_addr = addr;
    log::async().info("Accepted connection from {}", client_addr.toString());
    co_await coro::thread::dispatch_awaiter{};

    char read_buffer[8192];
    std::optional<std::string_view> remain = std::nullopt;
    while (true) {
        auto parser = http::req_msg::parser();
        if (remain.has_value()) {
            parser.send_and_resume(remain.value());
            remain = std::nullopt;
        }
        while (!parser.done()) {       
            auto res = co_await io_link_timeout_awaiter{
                io_read_awaiter{fd_w, read_buffer, sizeof(read_buffer)},
                60s
            };
            
            if (!res.has_value()) {
                log::async().warn("Read timed out or failed");
                co_return;
            }

            auto bytes_read = res.value().res;
            if (bytes_read <= 0) {
                log::async().warn("Connection closed by client or error occurred");
                co_return;
            }
            parser.send_and_resume({read_buffer, static_cast<size_t>(bytes_read)});
        }

        if (auto result = parser.get()) {
            auto& [msg, remain_opt] = result.value();
            remain = remain_opt;

            switch (msg.req_l.method) {
                case http::req_line::method_t::GET: {
                    auto get_res = handle_get_req(msg);
                    if (get_res.has_value()) {

                        auto res = co_await io_link_timeout_awaiter{
                            io_writev_awaiter{
                                fd_w,
                                &get_res.value().header,
                                2
                            },
                            5s
                        };

                        if (!res.has_value()) {
                            log::async().error("Failed to send response");
                            co_return;
                        }

                    } else {
                        co_await send_http_error{fd_w, get_res.error()};
                    }
                }
                break;
                default: {
                    co_await send_http_error{fd_w, http::error_code::method_not_allowed};
                    co_return;
                }
            
            }
        } else {
            co_await send_http_error{fd_w, http::error_code::bad_request};
            co_return;            
        }

    }
}



coro::task server_loop(net::ipv4 addr) {
    auto fd_w = setup_socket(addr);

    if (!fd_w.is_valid()){
        log::async().error("Failed to create socket");
        co_return;
    }
    log::async().info("Server listening on {}", addr.toString());
    co_await coro::thread::dispatch_awaiter{};

    while(true){
        sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);

        auto res = co_await io_link_timeout_awaiter{
            io_accept_awaiter{fd_w, (sockaddr *)&client_addr, &client_addr_len},
            5s
        };

        if (!res.has_value()){
            log::async().info("Accept timed out or failed");
            continue;
        }

        async_handle_connection(res.value().res, net::ipv4::from_sockaddr_in(client_addr));
    }

    co_return;
}